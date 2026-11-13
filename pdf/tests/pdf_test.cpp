// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/pdf/document.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

using namespace cutefish;
using cutefish::pdf::Document;

namespace {

const std::filesystem::path kDirectory = "/tmp/cutefish-pdf-test-" + std::to_string(getpid());

std::string stream(const std::string& contents)
{
    return "<< /Length " + std::to_string(contents.size()) + " >>\nstream\n" + contents + "\nendstream";
}

// A PDF of these objects, numbered from 1, the first the catalog.
std::string pdfOf(const std::vector<std::string>& objects)
{
    std::string out = "%PDF-1.7\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const size_t offset : offsets) {
        char line[21];
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
            + std::to_string(xref) + "\n%%EOF\n";
    return out;
}

// Page 1: 200 x 100, a red square at its top left, "Hello Cutefish", a link
// to a web address and one to page 2. Page 2: 300 x 400 turned a quarter.
// An outline of two items, the second with a child.
std::string samplePdf()
{
    return pdfOf({
        "<< /Type /Catalog /Pages 2 0 R /Outlines 9 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents 5 0 R "
        "/Resources << /Font << /F1 6 0 R >> >> /Annots [7 0 R 8 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Rotate 90 /Contents 12 0 R >>",
        stream("1 0 0 rg 0 80 20 20 re f BT /F1 12 Tf 30 50 Td (Hello Cutefish) Tj ET"),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Annot /Subtype /Link /Rect [100 0 200 30] /Border [0 0 0] "
        "/A << /S /URI /URI (https://cutefish.org) >> >>",
        "<< /Type /Annot /Subtype /Link /Rect [100 70 200 100] /Border [0 0 0] /Dest [4 0 R /XYZ 0 400 0] >>",
        "<< /Type /Outlines /First 10 0 R /Last 11 0 R /Count 3 >>",
        "<< /Title (First) /Parent 9 0 R /Next 11 0 R /Dest [3 0 R /Fit] >>",
        "<< /Title (Second) /Parent 9 0 R /Prev 10 0 R /Dest [4 0 R /Fit] /First 13 0 R /Last 13 0 R /Count 1 >>",
        stream(""),
        "<< /Title (Child) /Parent 11 0 R /Dest [4 0 R /Fit] >>",
    });
}

std::string write(const std::string& name, const std::string& contents)
{
    std::filesystem::create_directories(kDirectory);
    const std::string path = (kDirectory / name).string();
    std::ofstream(path, std::ios::binary) << contents;
    return path;
}

std::unique_ptr<Document> openSample()
{
    Document::Opening opening = Document::open(write("sample.pdf", samplePdf()));
    EXPECT_EQ(opening.status, Document::Status::Opened);
    return std::move(opening.document);
}

uint32_t pixel(const pdf::Bitmap& bitmap, int x, int y)
{
    return bitmap.pixels[size_t(y) * size_t(bitmap.width) + size_t(x)];
}

class PdfTest : public testing::Test
{
protected:
    static void TearDownTestSuite() { std::filesystem::remove_all(kDirectory); }
};

} // namespace

TEST_F(PdfTest, PagesHaveTheirCountAndShownSize)
{
    const auto document = openSample();
    ASSERT_TRUE(document);
    EXPECT_EQ(document->pageCount(), 2);
    EXPECT_EQ(document->pageSize(0), base::SizeF(200, 100));
    EXPECT_EQ(document->pageSize(1), base::SizeF(400, 300));
}

TEST_F(PdfTest, PagesRenderAtAScaleAndInParts)
{
    const auto document = openSample();
    ASSERT_TRUE(document);
    const pdf::Bitmap whole = document->render(0, 1, base::Rect(0, 0, 200, 100));
    ASSERT_EQ(whole.width, 200);
    EXPECT_EQ(pixel(whole, 5, 5), 0xffff0000u);
    EXPECT_EQ(pixel(whole, 190, 90), 0xffffffffu);

    const pdf::Bitmap twice = document->render(0, 2, base::Rect(0, 0, 400, 200));
    EXPECT_EQ(pixel(twice, 35, 35), 0xffff0000u);
    EXPECT_EQ(pixel(twice, 45, 45), 0xffffffffu);

    // Pixels 20 to 59 of the page at twice the size.
    const pdf::Bitmap part = document->render(0, 2, base::Rect(20, 20, 40, 40));
    ASSERT_EQ(part.width, 40);
    EXPECT_EQ(pixel(part, 15, 15), 0xffff0000u);
    EXPECT_EQ(pixel(part, 25, 25), 0xffffffffu);

    const std::atomic<bool> cancelled = true;
    EXPECT_TRUE(document->render(0, 1, base::Rect(0, 0, 200, 100), &cancelled).isNull());
    EXPECT_TRUE(document->render(5, 1, base::Rect(0, 0, 10, 10)).isNull());
}

TEST_F(PdfTest, TextHasItsCharactersAndBoxes)
{
    const auto document = openSample();
    ASSERT_TRUE(document);
    const pdf::PageText text = document->text(0);
    ASSERT_EQ(text.text(0, text.characters.size()), "Hello Cutefish");
    const base::RectF h = text.characters[0].box;
    EXPECT_NEAR(h.left(), 30, 1.5);
    // The baseline is 50 above the bottom: 50 below the top of a 100 tall page.
    EXPECT_NEAR(h.bottom(), 50, 1.5);
    EXPECT_GT(h.height(), 5);

    const std::vector<pdf::Match> matches = document->find(0, "cutefish");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].first, 6u);
    EXPECT_EQ(matches[0].count, 8u);
    ASSERT_EQ(matches[0].rects.size(), 1u);
    EXPECT_GT(matches[0].rects[0].left(), h.right());
    EXPECT_TRUE(document->find(0, "cutefish", true).empty());
}

TEST_F(PdfTest, LinksGoToAPageOrAnAddress)
{
    const auto document = openSample();
    ASSERT_TRUE(document);
    const std::vector<pdf::Link> links = document->links(0);
    ASSERT_EQ(links.size(), 2u);
    EXPECT_EQ(links[0].uri, "https://cutefish.org");
    EXPECT_EQ(links[0].area, base::RectF(100, 70, 100, 30));
    EXPECT_EQ(links[1].destination.page, 1);
    EXPECT_EQ(links[1].area, base::RectF(100, 0, 100, 30));
}

TEST_F(PdfTest, TheOutlineIsATree)
{
    const auto document = openSample();
    ASSERT_TRUE(document);
    const std::vector<pdf::OutlineItem> outline = document->outline();
    ASSERT_EQ(outline.size(), 2u);
    EXPECT_EQ(outline[0].title, "First");
    EXPECT_EQ(outline[0].destination.page, 0);
    EXPECT_EQ(outline[1].title, "Second");
    EXPECT_EQ(outline[1].destination.page, 1);
    ASSERT_EQ(outline[1].children.size(), 1u);
    EXPECT_EQ(outline[1].children[0].title, "Child");
    EXPECT_EQ(document->pageSizes(), (std::vector<base::SizeF> {{200, 100}, {400, 300}}));
}

TEST_F(PdfTest, StandardFontsDrawWhateverFontsTheSystemHas)
{
    const auto document = openSample();
    ASSERT_TRUE(document);
    // "Hello Cutefish" in Helvetica, not embedded: PDFium's own copy of the
    // standard fonts draws it.
    const pdf::Bitmap page = document->render(0, 2, base::Rect(0, 0, 400, 200));
    int ink = 0;
    for (int y = 70; y < 105; ++y)
        for (int x = 60; x < 250; ++x)
            ink += (pixel(page, x, y) & 0xff) < 128;
    EXPECT_GT(ink, 200);
}

// The text "Turn" at (130, 250), a link over (150, 220)-(250, 240) to a point
// of the page, on a page whose crop box is (100, 200)-(300, 300) of a larger
// media box, turned by `rotation`.
static std::string turnedPdf(int rotation)
{
    return pdfOf({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /CropBox [100 200 300 300] /Rotate "
                + std::to_string(rotation)
                + " /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> /Annots [6 0 R] >>",
        stream("BT /F1 12 Tf 130 250 Td (Turn) Tj ET"),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Annot /Subtype /Link /Rect [150 220 250 240] /Border [0 0 0] /Dest [3 0 R /XYZ 120 280 0] >>",
    });
}

TEST_F(PdfTest, TurnedAndCroppedPagesShowTheirPositions)
{
    struct Expected
    {
        int rotation;
        base::SizeF size;
        // The baseline's start of "T", its link, and the destination's point.
        base::PointF baseline;
        base::RectF link;
        base::PointF destination;
    };
    for (const Expected& expected : {
             Expected {0, {200, 100}, {30, 50}, {50, 60, 100, 20}, {20, 20}},
             Expected {90, {100, 200}, {50, 30}, {20, 50, 20, 100}, {80, 20}},
             Expected {270, {100, 200}, {50, 170}, {60, 50, 20, 100}, {20, 180}},
         }) {
        SCOPED_TRACE(expected.rotation);
        Document::Opening opening = Document::open(write("turned.pdf", turnedPdf(expected.rotation)));
        ASSERT_EQ(opening.status, Document::Status::Opened);
        const Document& document = *opening.document;
        EXPECT_EQ(document.pageSize(0), expected.size);

        const pdf::PageText text = document.text(0);
        ASSERT_EQ(text.text(0, 4), "Turn");
        const base::RectF t = text.characters[0].box;
        // The glyph's box starts at the baseline's start and grows up from it,
        // which turns with the page.
        const double along = expected.rotation == 0 ? t.left() : expected.rotation == 90 ? t.top() : t.bottom();
        const double across = expected.rotation == 0 ? t.bottom() : expected.rotation == 90 ? t.left() : t.right();
        EXPECT_NEAR(along, expected.rotation == 0 ? expected.baseline.x() : expected.baseline.y(), 1.5);
        EXPECT_NEAR(across, expected.rotation == 0 ? expected.baseline.y() : expected.baseline.x(), 1.5);

        const std::vector<pdf::Match> matches = document.find(0, "turn");
        ASSERT_EQ(matches.size(), 1u);
        ASSERT_EQ(matches[0].rects.size(), 1u);
        const base::RectF match = matches[0].rects[0];
        for (size_t i = 0; i < 4; ++i) {
            const base::RectF box = text.characters[i].box;
            EXPECT_GE(box.left(), match.left() - 0.5);
            EXPECT_LE(box.right(), match.right() + 0.5);
            EXPECT_GE(box.top(), match.top() - 0.5);
            EXPECT_LE(box.bottom(), match.bottom() + 0.5);
        }

        const std::vector<pdf::Link> links = document.links(0);
        ASSERT_EQ(links.size(), 1u);
        EXPECT_EQ(links[0].area, expected.link);
        const base::PointF point = document.position(links[0].destination);
        EXPECT_NEAR(point.x(), expected.destination.x(), 0.01);
        EXPECT_NEAR(point.y(), expected.destination.y(), 0.01);
    }
}

// protected.pdf: "Secret page", user password "secret", from Ghostscript's
// pdfwrite with 128-bit RC4.
TEST_F(PdfTest, APasswordIsAskedForAndChecked)
{
    const std::string path = PDF_TEST_DATA_DIR "/protected.pdf";
    EXPECT_EQ(Document::open(path).status, Document::Status::PasswordRequired);
    EXPECT_EQ(Document::open(path, "wrong").status, Document::Status::PasswordWrong);
    Document::Opening opening = Document::open(path, "secret");
    ASSERT_EQ(opening.status, Document::Status::Opened);
    const pdf::PageText text = opening.document->text(0);
    EXPECT_NE(text.text(0, text.characters.size()).find("Secret page"), std::string::npos);
}

TEST_F(PdfTest, DamagedAndMissingFilesAreUnreadable)
{
    EXPECT_EQ(Document::open(write("short.pdf", samplePdf().substr(0, 40))).status, Document::Status::Unreadable);
    EXPECT_EQ(Document::open(write("text.pdf", "not a PDF at all")).status, Document::Status::Unreadable);
    EXPECT_EQ(Document::open((kDirectory / "missing.pdf").string()).status, Document::Status::Unreadable);
    // Cut after its pages: what can be read of it is read, without a crash.
    const std::string sample = samplePdf();
    Document::Opening cut = Document::open(write("cut.pdf", sample.substr(0, sample.find("8 0 obj"))));
    if (cut.document) {
        for (int page = 0; page < cut.document->pageCount(); ++page)
            cut.document->render(page, 1, base::Rect(0, 0, 50, 50));
    }
}

TEST_F(PdfTest, CallsFromSeveralThreadsTakeTurns)
{
    const auto document = openSample();
    ASSERT_TRUE(document);
    std::vector<std::thread> threads;
    std::atomic<int> rendered = 0;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < 10; ++j) {
                if (!document->render(j % 2, 1.5, base::Rect(0, 0, 100, 100)).isNull())
                    ++rendered;
                document->text(0);
            }
        });
    }
    for (std::thread& thread : threads)
        thread.join();
    EXPECT_EQ(rendered, 40);
}
