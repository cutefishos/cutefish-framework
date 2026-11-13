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

struct Link
{
    base::RectF area;
    // A destination in the document: its page, and the point on it when the
    // link names one.
    int page = -1;
    std::optional<base::PointF> point;
    // Else a web address, or another URI.
    std::string uri;
};

struct OutlineItem
{
    std::string title;
    // -1 when the item goes nowhere in the document.
    int page = -1;
    std::vector<OutlineItem> children;
};

// A PDF file, read with PDFium. PDFium keeps state for the whole process and
// is not thread-safe, so every call into it, from any Document on any thread,
// takes one process-wide lock: calls are safe from anywhere and run one at a
// time. A caller wanting a page drawn without the others waiting renders on
// a thread of its own.
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
    std::vector<Link> links(int page) const;
    std::vector<OutlineItem> outline() const;

private:
    struct Private;

    explicit Document(std::unique_ptr<Private> d);

    std::unique_ptr<Private> d;
};

} // namespace cutefish::pdf
