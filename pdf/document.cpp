// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/pdf/document.h"
#include "system_fonts_p.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <mutex>

#include "public/fpdf_doc.h"
#include "public/fpdf_formfill.h"
#include "public/fpdf_edit.h"
#include "public/fpdf_progressive.h"
#include "public/fpdf_sysfontinfo.h"
#include "public/fpdf_text.h"
#include "public/fpdfview.h"

namespace cutefish::pdf {

// Pages kept parsed: a page shown as several tiles also gives its text and
// links, and its neighbours are near.
static constexpr size_t kCachedPages = 6;
// An outline from a damaged or hostile file may loop or never end.
static constexpr int kOutlineDepth = 32;
static constexpr size_t kOutlineItems = 10000;

static std::mutex& pdfiumLock()
{
    static std::mutex lock;
    return lock;
}

// Once per process, under the lock.
static void initialisePdfium()
{
    static bool initialised = false;
    if (initialised)
        return;
    FPDF_LIBRARY_CONFIG config {};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
    FPDF_SetSystemFontInfo(systemFonts());
    initialised = true;
}

static std::string utf8(const std::u32string& text)
{
    std::string out;
    for (const char32_t c : text) {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += char(0xe0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3f));
            out += char(0x80 | (c & 0x3f));
        } else {
            out += char(0xf0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 0x3f));
            out += char(0x80 | ((c >> 6) & 0x3f));
            out += char(0x80 | (c & 0x3f));
        }
    }
    return out;
}

// UTF-16LE code units, as PDFium writes and reads text, to UTF-32.
static std::u32string fromUtf16(const std::vector<unsigned short>& units)
{
    std::u32string out;
    for (size_t i = 0; i < units.size() && units[i]; ++i) {
        char32_t c = units[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < units.size() && units[i + 1] >= 0xdc00 && units[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (units[++i] - 0xdc00);
        out += c;
    }
    return out;
}

static std::vector<unsigned short> toUtf16(const std::string& text)
{
    std::vector<unsigned short> out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char lead = text[i];
        char32_t c = lead;
        int more = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
        if (more)
            c = lead & (0x3f >> more);
        ++i;
        for (; more > 0 && i < text.size(); --more, ++i)
            c = c << 6 | (static_cast<unsigned char>(text[i]) & 0x3f);
        if (c >= 0x10000) {
            out.push_back(static_cast<unsigned short>(0xd800 + ((c - 0x10000) >> 10)));
            out.push_back(static_cast<unsigned short>(0xdc00 + ((c - 0x10000) & 0x3ff)));
        } else {
            out.push_back(static_cast<unsigned short>(c));
        }
    }
    out.push_back(0);
    return out;
}

std::string PageText::text(size_t first, size_t count) const
{
    std::u32string text;
    for (size_t i = first; i < std::min(characters.size(), first + count); ++i)
        text += characters[i].code;
    return utf8(text);
}

namespace {

// A parsed page, its text when asked for, and the conversion of its own
// coordinates to the page as shown: its box's top left at 0, its /Rotate
// applied, clockwise.
class Page
{
public:
    Page(FPDF_DOCUMENT document, FPDF_FORMHANDLE form, int index)
        : index(index)
        , m_page(FPDF_LoadPage(document, index))
        , m_form(form)
    {
        if (!m_page)
            return;
        if (m_form)
            FORM_OnAfterLoadPage(m_page, m_form);
        FS_RECTF box {};
        FPDF_GetPageBoundingBox(m_page, &box);
        m_left = box.left;
        m_right = box.right;
        m_top = box.top;
        m_bottom = box.bottom;
        m_rotation = std::clamp(FPDFPage_GetRotation(m_page), 0, 3);
    }

    ~Page()
    {
        if (!m_page)
            return;
        if (m_text)
            FPDFText_ClosePage(m_text);
        if (m_form)
            FORM_OnBeforeClosePage(m_page, m_form);
        FPDF_ClosePage(m_page);
    }

    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    explicit operator bool() const { return m_page != nullptr; }
    FPDF_PAGE get() const { return m_page; }
    double width() const { return FPDF_GetPageWidthF(m_page); }
    double height() const { return FPDF_GetPageHeightF(m_page); }
    double boxLeft() const { return m_left; }
    double boxTop() const { return m_top; }

    FPDF_TEXTPAGE text()
    {
        if (!m_text)
            m_text = FPDFText_LoadPage(m_page);
        return m_text;
    }

    base::PointF toShown(double x, double y) const
    {
        switch (m_rotation) {
        case 1:
            return {y - m_bottom, x - m_left};
        case 2:
            return {m_right - x, y - m_bottom};
        case 3:
            return {m_top - y, m_right - x};
        default:
            return {x - m_left, m_top - y};
        }
    }

    base::RectF toShown(double left, double top, double right, double bottom) const
    {
        return base::RectF::boundingRect(toShown(left, top), toShown(right, bottom));
    }

    const int index;

private:
    FPDF_PAGE m_page;
    FPDF_FORMHANDLE m_form;
    FPDF_TEXTPAGE m_text = nullptr;
    double m_left = 0;
    double m_right = 0;
    double m_top = 0;
    double m_bottom = 0;
    int m_rotation = 0;
};

// The renderer's pause callback: asked between parts of the work.
struct Pause : IFSDK_PAUSE
{
    const std::atomic<bool>* cancelled;
};

} // namespace

struct Document::Private
{
    FPDF_DOCUMENT document = nullptr;
    FPDF_FORMHANDLE form = nullptr;
    FPDF_FORMFILLINFO formInfo {};
    // Most recently used first.
    std::list<std::unique_ptr<Page>> pages;

    // Under the lock. Null when there is no such page.
    Page* page(int index)
    {
        if (index < 0 || index >= FPDF_GetPageCount(document))
            return nullptr;
        for (auto it = pages.begin(); it != pages.end(); ++it) {
            if ((*it)->index == index) {
                pages.splice(pages.begin(), pages, it);
                return pages.front().get();
            }
        }
        auto page = std::make_unique<Page>(document, form, index);
        if (!*page)
            return nullptr;
        pages.push_front(std::move(page));
        if (pages.size() > kCachedPages)
            pages.pop_back();
        return pages.front().get();
    }
};

Document::Document(std::unique_ptr<Private> d)
    : d(std::move(d))
{
}

Document::~Document()
{
    const std::lock_guard lock(pdfiumLock());
    // Pages go before the form environment they were loaded into.
    d->pages.clear();
    if (d->form)
        FPDFDOC_ExitFormFillEnvironment(d->form);
    FPDF_CloseDocument(d->document);
}

Document::Opening Document::open(const std::string& path, const std::string& password)
{
    const std::lock_guard lock(pdfiumLock());
    initialisePdfium();
    FPDF_DOCUMENT document = FPDF_LoadDocument(path.c_str(), password.empty() ? nullptr : password.c_str());
    if (!document) {
        if (FPDF_GetLastError() == FPDF_ERR_PASSWORD)
            return {password.empty() ? Status::PasswordRequired : Status::PasswordWrong, nullptr};
        return {Status::Unreadable, nullptr};
    }
    auto d = std::make_unique<Private>();
    d->document = document;
    // Version 1: no XFA, and no JavaScript, which this PDFium has no engine for.
    d->formInfo.version = 1;
    d->form = FPDFDOC_InitFormFillEnvironment(document, &d->formInfo);
    return {Status::Opened, std::unique_ptr<Document>(new Document(std::move(d)))};
}

int Document::pageCount() const
{
    const std::lock_guard lock(pdfiumLock());
    return FPDF_GetPageCount(d->document);
}

base::SizeF Document::pageSize(int page) const
{
    const std::lock_guard lock(pdfiumLock());
    FS_SIZEF size {};
    if (!FPDF_GetPageSizeByIndexF(d->document, page, &size))
        return {};
    return {size.width, size.height};
}

std::vector<base::SizeF> Document::pageSizes() const
{
    const std::lock_guard lock(pdfiumLock());
    std::vector<base::SizeF> sizes;
    const int count = FPDF_GetPageCount(d->document);
    sizes.reserve(size_t(std::max(0, count)));
    for (int page = 0; page < count; ++page) {
        FS_SIZEF size {};
        FPDF_GetPageSizeByIndexF(d->document, page, &size);
        sizes.emplace_back(size.width, size.height);
    }
    return sizes;
}

static FPDF_BOOL needsToPause(IFSDK_PAUSE* pause)
{
    const std::atomic<bool>* cancelled = static_cast<Pause*>(pause)->cancelled;
    return cancelled && cancelled->load();
}

Bitmap Document::render(int index, double scale, const base::Rect& region, const std::atomic<bool>* cancelled) const
{
    if (region.isEmpty() || (cancelled && cancelled->load()))
        return {};
    const std::lock_guard lock(pdfiumLock());
    Page* page = d->page(index);
    if (!page || (cancelled && cancelled->load()))
        return {};
    const int pageWidth = int(std::lround(page->width() * scale));
    const int pageHeight = int(std::lround(page->height() * scale));
    Bitmap bitmap;
    bitmap.width = region.width();
    bitmap.height = region.height();
    bitmap.pixels.resize(size_t(bitmap.width) * size_t(bitmap.height));
    FPDF_BITMAP target = FPDFBitmap_CreateEx(bitmap.width, bitmap.height, FPDFBitmap_BGRA, bitmap.pixels.data(),
                                             bitmap.width * 4);
    if (!target)
        return {};
    FPDFBitmap_FillRect(target, 0, 0, bitmap.width, bitmap.height, 0xffffffff);
    Pause pause {};
    pause.version = 1;
    pause.NeedToPauseNow = needsToPause;
    pause.cancelled = cancelled;
    int status = FPDF_RenderPageBitmap_Start(target, page->get(), -region.x(), -region.y(), pageWidth, pageHeight,
                                             0, FPDF_ANNOT, &pause);
    while (status == FPDF_RENDER_TOBECONTINUED && !(cancelled && cancelled->load()))
        status = FPDF_RenderPage_Continue(page->get(), &pause);
    FPDF_RenderPage_Close(page->get());
    const bool done = status == FPDF_RENDER_DONE;
    if (done && d->form)
        FPDF_FFLDraw(d->form, target, page->get(), -region.x(), -region.y(), pageWidth, pageHeight, 0, FPDF_ANNOT);
    FPDFBitmap_Destroy(target);
    return done ? bitmap : Bitmap();
}

PageText Document::text(int index) const
{
    const std::lock_guard lock(pdfiumLock());
    PageText result;
    Page* page = d->page(index);
    FPDF_TEXTPAGE text = page ? page->text() : nullptr;
    if (!text)
        return result;
    const int count = FPDFText_CountChars(text);
    result.characters.reserve(size_t(std::max(0, count)));
    for (int i = 0; i < count; ++i) {
        Character character;
        character.code = FPDFText_GetUnicode(text, i);
        double left = 0, right = 0, bottom = 0, top = 0;
        if (!FPDFText_IsGenerated(text, i) && FPDFText_GetCharBox(text, i, &left, &right, &bottom, &top))
            character.box = page->toShown(left, top, right, bottom);
        result.characters.push_back(character);
    }
    return result;
}

std::vector<Match> Document::find(int index, const std::string& query, bool matchCase) const
{
    std::vector<Match> matches;
    if (query.empty())
        return matches;
    const std::lock_guard lock(pdfiumLock());
    Page* page = d->page(index);
    FPDF_TEXTPAGE text = page ? page->text() : nullptr;
    if (!text)
        return matches;
    std::vector<unsigned short> needle = toUtf16(query);
    FPDF_SCHHANDLE search = FPDFText_FindStart(text, needle.data(), matchCase ? FPDF_MATCHCASE : 0, 0);
    if (!search)
        return matches;
    while (FPDFText_FindNext(search)) {
        Match match;
        match.first = size_t(FPDFText_GetSchResultIndex(search));
        match.count = size_t(FPDFText_GetSchCount(search));
        const int rects = FPDFText_CountRects(text, int(match.first), int(match.count));
        for (int i = 0; i < rects; ++i) {
            double left = 0, top = 0, right = 0, bottom = 0;
            if (FPDFText_GetRect(text, i, &left, &top, &right, &bottom))
                match.rects.push_back(page->toShown(left, top, right, bottom));
        }
        matches.push_back(std::move(match));
    }
    FPDFText_FindClose(search);
    return matches;
}

// The page and the place on it the destination names, without reading the page.
static Destination destinationOf(FPDF_DOCUMENT document, FPDF_DEST dest)
{
    Destination destination;
    destination.page = FPDFDest_GetDestPageIndex(document, dest);
    FPDF_BOOL hasX = false, hasY = false, hasZoom = false;
    FS_FLOAT x = 0, y = 0, zoom = 0;
    if (destination.page >= 0 && FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &x, &y, &zoom)) {
        destination.hasX = hasX;
        destination.hasY = hasY;
        destination.x = x;
        destination.y = y;
    }
    return destination;
}

// An action's destination, or URI.
static void follow(FPDF_DOCUMENT document, FPDF_ACTION action, Destination* destination, std::string* uri)
{
    switch (FPDFAction_GetType(action)) {
    case PDFACTION_GOTO:
        if (FPDF_DEST dest = FPDFAction_GetDest(document, action))
            *destination = destinationOf(document, dest);
        break;
    case PDFACTION_URI: {
        const unsigned long length = FPDFAction_GetURIPath(document, action, nullptr, 0);
        std::string text(length, '\0');
        FPDFAction_GetURIPath(document, action, text.data(), length);
        // The length counts the terminating NUL.
        *uri = std::string(text.c_str());
        break;
    }
    default:
        break;
    }
}

std::vector<Link> Document::links(int index) const
{
    std::vector<Link> links;
    const std::lock_guard lock(pdfiumLock());
    Page* page = d->page(index);
    if (!page)
        return links;
    int position = 0;
    FPDF_LINK link = nullptr;
    while (FPDFLink_Enumerate(page->get(), &position, &link)) {
        Link entry;
        FS_RECTF area {};
        if (!FPDFLink_GetAnnotRect(link, &area))
            continue;
        entry.area = page->toShown(area.left, area.top, area.right, area.bottom);
        if (FPDF_DEST dest = FPDFLink_GetDest(d->document, link))
            entry.destination = destinationOf(d->document, dest);
        else if (FPDF_ACTION action = FPDFLink_GetAction(link))
            follow(d->document, action, &entry.destination, &entry.uri);
        if (entry.destination.page >= 0 || !entry.uri.empty())
            links.push_back(std::move(entry));
    }
    return links;
}

static std::vector<OutlineItem> outlineBelow(FPDF_DOCUMENT document, FPDF_BOOKMARK parent, int depth, size_t* items)
{
    std::vector<OutlineItem> result;
    if (depth > kOutlineDepth)
        return result;
    for (FPDF_BOOKMARK bookmark = FPDFBookmark_GetFirstChild(document, parent);
         bookmark && *items < kOutlineItems; bookmark = FPDFBookmark_GetNextSibling(document, bookmark)) {
        ++*items;
        OutlineItem item;
        const unsigned long bytes = FPDFBookmark_GetTitle(bookmark, nullptr, 0);
        std::vector<unsigned short> title(bytes / 2 + 1, 0);
        FPDFBookmark_GetTitle(bookmark, title.data(), bytes);
        item.title = utf8(fromUtf16(title));
        std::string uri;
        if (FPDF_DEST dest = FPDFBookmark_GetDest(document, bookmark))
            item.destination = destinationOf(document, dest);
        else if (FPDF_ACTION action = FPDFBookmark_GetAction(bookmark))
            follow(document, action, &item.destination, &uri);
        item.children = outlineBelow(document, bookmark, depth + 1, items);
        result.push_back(std::move(item));
    }
    return result;
}

std::vector<OutlineItem> Document::outline() const
{
    const std::lock_guard lock(pdfiumLock());
    size_t items = 0;
    return outlineBelow(d->document, nullptr, 0, &items);
}

base::PointF Document::position(const Destination& destination) const
{
    const std::lock_guard lock(pdfiumLock());
    const Page* page = d->page(destination.page);
    if (!page)
        return {};
    // A coordinate the destination leaves out is the page box's left or top.
    const base::PointF point = page->toShown(destination.hasX ? destination.x : page->boxLeft(),
                                             destination.hasY ? destination.y : page->boxTop());
    return {std::max(0.0, point.x()), std::max(0.0, point.y())};
}

} // namespace cutefish::pdf
