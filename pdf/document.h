// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/geometry.h"

namespace cutefish::pdf {

// Positions and sizes are in PDF points (1/72 inch) on the page as it is
// shown, its /Rotate applied, with the origin at its top left.

// Pixels as 0xAARRGGBB words, row by row, premultiplied.
struct Bitmap
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    bool isNull() const { return pixels.empty(); }
};

// A character of a page's text, as its reading order has it: spaces and
// line breaks the layout implies are characters too, with an empty box.
struct Character
{
    char32_t code = 0;
    base::RectF box;
};

struct PageText
{
    std::vector<Character> characters;

    // UTF-8 of `count` characters from `first`.
    std::string text(size_t first, size_t count) const;
};

struct Match
{
    // In the page's characters.
    size_t first = 0;
    size_t count = 0;
    // A rect per line the match covers.
    std::vector<base::RectF> rects;
};

// A place in the document: a page, and where on it as the file gives it,
// which Document::position() turns into a point on the page as shown.
struct Destination
{
    // -1 for none.
    int page = -1;
    bool hasX = false;
    bool hasY = false;
    // In the page's own PDF coordinates.
    double x = 0;
    double y = 0;
};

struct Link
{
    base::RectF area;
    // In the document, or else a web address or another URI.
    Destination destination;
    std::string uri;
};

struct OutlineItem
{
    std::string title;
    Destination destination;
    std::vector<OutlineItem> children;
};

// A PDF file, read with PDFium. PDFium keeps state for the whole process and
// is not thread-safe, so every call into it, from any Document on any thread,
// takes one process-wide lock: calls are safe from anywhere and run one at a
// time, and may wait behind a long render; a UI calls from a worker. The
// last few pages used stay parsed. Fonts a file does not embed come from
// fontconfig's matching.
class Document
{
public:
    enum class Status {
        Opened,
        // Encrypted with a user password, and none or another one was given.
        PasswordRequired,
        PasswordWrong,
        // Missing, not a PDF, or damaged beyond reading.
        Unreadable,
    };

    struct Opening
    {
        Status status = Status::Unreadable;
        // Set when opened.
        std::unique_ptr<Document> document;
    };

    static Opening open(const std::string& path, const std::string& password = {});
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    int pageCount() const;
    base::SizeF pageSize(int page) const;
    // Every page's, in one go.
    std::vector<base::SizeF> pageSizes() const;

    // The part `region` of the page drawn `scale` pixels to a point, on
    // white, with its annotations and filled form fields; `region` is in
    // pixels of the whole page at that scale. Null when the page or region
    // is not there, or when `cancelled` became true while drawing, which is
    // looked at between parts of the work.
    Bitmap render(int page, double scale, const base::Rect& region,
                  const std::atomic<bool>* cancelled = nullptr) const;

    PageText text(int page) const;
    // Every occurrence on the page, in reading order.
    std::vector<Match> find(int page, const std::string& query, bool matchCase = false) const;
    // Neither reads any page but the one asked for.
    std::vector<Link> links(int page) const;
    std::vector<OutlineItem> outline() const;
    // The destination's point on its page as shown: its top left where the
    // destination does not say.
    base::PointF position(const Destination& destination) const;

private:
    struct Private;

    explicit Document(std::unique_ptr<Private> d);

    std::unique_ptr<Private> d;
};

} // namespace cutefish::pdf
