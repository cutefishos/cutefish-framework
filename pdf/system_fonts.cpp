// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "system_fonts_p.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fontconfig/fontconfig.h>

#include "public/fpdf_sysfontinfo.h"

namespace cutefish::pdf {

static constexpr uint32_t kTableTtcf = 0x74746366;

namespace {

// A font file, and in a collection the face's place in it.
struct Font
{
    std::string path;
    std::string family;
    int charset = FXFONT_ANSI_CHARSET;
    uint32_t fileSize = 0;
    // Where the face's table directory starts: 0 outside a collection.
    uint32_t faceOffset = 0;
    // The directory's entries, 16 bytes each.
    std::vector<uint8_t> tables;
};

} // namespace

static uint32_t be32(const uint8_t* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

static bool readAt(FILE* file, uint32_t offset, uint8_t* out, size_t size)
{
    return std::fseek(file, long(offset), SEEK_SET) == 0 && std::fread(out, 1, size, file) == size;
}

// The face `index` of the file: its offset and table directory.
static bool readFace(Font& font, int index)
{
    FILE* file = std::fopen(font.path.c_str(), "rb");
    if (!file)
        return false;
    uint8_t header[12];
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    font.fileSize = uint32_t(std::ftell(file));
    ok = ok && readAt(file, 0, header, sizeof(header));
    if (ok && be32(header) == kTableTtcf) {
        uint8_t offset[4];
        ok = uint32_t(index) < be32(header + 8) && readAt(file, 12 + 4 * uint32_t(index), offset, 4);
        font.faceOffset = ok ? be32(offset) : 0;
        ok = ok && readAt(file, font.faceOffset, header, sizeof(header));
    }
    if (ok) {
        const uint32_t count = uint32_t(header[4]) << 8 | header[5];
        font.tables.resize(size_t(count) * 16);
        ok = readAt(file, font.faceOffset + 12, font.tables.data(), font.tables.size());
    }
    std::fclose(file);
    return ok;
}

// The language whose fonts carry the character set's characters.
static const char* languageOf(int charset)
{
    switch (charset) {
    case FXFONT_SHIFTJIS_CHARSET:
        return "ja";
    case FXFONT_HANGEUL_CHARSET:
        return "ko";
    case FXFONT_GB2312_CHARSET:
        return "zh-cn";
    case FXFONT_CHINESEBIG5_CHARSET:
        return "zh-tw";
    case FXFONT_GREEK_CHARSET:
        return "el";
    case FXFONT_HEBREW_CHARSET:
        return "he";
    case FXFONT_ARABIC_CHARSET:
        return "ar";
    case FXFONT_CYRILLIC_CHARSET:
        return "ru";
    case FXFONT_THAI_CHARSET:
        return "th";
    case FXFONT_VIETNAMESE_CHARSET:
        return "vi";
    default:
        return nullptr;
    }
}

// Song, Ming and Mincho faces are the CJK serif ones.
static bool serifName(const std::string& family)
{
    for (const char* part : {"Song", "Ming", "Mincho", "Serif", "Times", "Roman", "Georgia", "Garamond"}) {
        if (family.find(part) != std::string::npos)
            return true;
    }
    return false;
}

// The best match that has the language's characters: a preferred family
// without them, such as a Latin interface font, would draw nothing.
static FcPattern* coveringMatch(FcPattern* pattern, const char* language)
{
    FcResult result = FcResultNoMatch;
    FcFontSet* fonts = FcFontSort(nullptr, pattern, FcTrue, nullptr, &result);
    if (!fonts)
        return nullptr;
    FcPattern* match = nullptr;
    for (int i = 0; i < fonts->nfont && !match; ++i) {
        FcLangSet* languages = nullptr;
        if (FcPatternGetLangSet(fonts->fonts[i], FC_LANG, 0, &languages) == FcResultMatch
            && FcLangSetHasLang(languages, reinterpret_cast<const FcChar8*>(language)) != FcLangDifferentLang)
            match = FcFontRenderPrepare(nullptr, pattern, fonts->fonts[i]);
    }
    FcFontSetDestroy(fonts);
    return match;
}

static void* mapFont(FPDF_SYSFONTINFO*, int weight, FPDF_BOOL italic, int charset, int pitchFamily,
                     const char* face, FPDF_BOOL* exact)
{
    // "Name,Bold" and "Name-Italic" carry the style in the name.
    std::string family = face ? face : "";
    const size_t comma = family.find(',');
    if (comma != std::string::npos)
        family.resize(comma);
    FcPattern* pattern = FcPatternCreate();
    if (!family.empty())
        FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    FcPatternAddInteger(pattern, FC_WEIGHT, FcWeightFromOpenType(weight > 0 ? weight : FXFONT_FW_NORMAL));
    FcPatternAddInteger(pattern, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    if (pitchFamily & FXFONT_FF_FIXEDPITCH)
        FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);
    // An unknown family falls back to the sort of type the name says.
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(serifName(family) ? "serif" : "sans-serif"));
    const char* language = languageOf(charset);
    if (language)
        FcPatternAddString(pattern, FC_LANG, reinterpret_cast<const FcChar8*>(language));
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcPattern* match = language ? coveringMatch(pattern, language) : nullptr;
    FcResult result = FcResultNoMatch;
    if (!match)
        match = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    if (!match)
        return nullptr;
    FcChar8* path = nullptr;
    FcChar8* matched = nullptr;
    int index = 0;
    Font* font = nullptr;
    if (FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch) {
        font = new Font;
        font->path = reinterpret_cast<const char*>(path);
        FcPatternGetInteger(match, FC_INDEX, 0, &index);
        if (FcPatternGetString(match, FC_FAMILY, 0, &matched) == FcResultMatch)
            font->family = reinterpret_cast<const char*>(matched);
        font->charset = charset;
        if (!readFace(*font, index)) {
            delete font;
            font = nullptr;
        }
    }
    FcPatternDestroy(match);
    if (exact && font)
        *exact = !family.empty() && FcStrCmpIgnoreCase(reinterpret_cast<const FcChar8*>(family.c_str()),
                                                       reinterpret_cast<const FcChar8*>(font->family.c_str()))
                        == 0;
    return font;
}

static void* getFont(FPDF_SYSFONTINFO* info, const char* face)
{
    return mapFont(info, FXFONT_FW_NORMAL, false, FXFONT_DEFAULT_CHARSET, 0, face, nullptr);
}

// As PDFium's own folder fonts answer: the whole file for table 0 outside a
// collection and for 'ttcf' inside one, else one table of the face.
static unsigned long getFontData(FPDF_SYSFONTINFO*, void* handle, unsigned int table, unsigned char* buffer,
                                 unsigned long size)
{
    const Font* font = static_cast<const Font*>(handle);
    if (!font)
        return 0;
    uint32_t offset = 0;
    uint32_t length = 0;
    if (table == 0) {
        length = font->faceOffset ? 0 : font->fileSize;
    } else if (table == kTableTtcf) {
        length = font->faceOffset ? font->fileSize : 0;
    } else {
        for (size_t entry = 0; entry + 16 <= font->tables.size(); entry += 16) {
            if (be32(&font->tables[entry]) == table) {
                offset = be32(&font->tables[entry + 8]);
                length = be32(&font->tables[entry + 12]);
                break;
            }
        }
    }
    if (!length || !buffer || size < length)
        return length;
    FILE* file = std::fopen(font->path.c_str(), "rb");
    if (!file)
        return 0;
    const bool read = readAt(file, offset, buffer, length);
    std::fclose(file);
    return read ? length : 0;
}

static unsigned long getFaceName(FPDF_SYSFONTINFO*, void* handle, char* buffer, unsigned long size)
{
    const std::string& family = static_cast<const Font*>(handle)->family;
    if (buffer && size > family.size())
        std::memcpy(buffer, family.c_str(), family.size() + 1);
    return family.size() + 1;
}

static int getFontCharset(FPDF_SYSFONTINFO*, void* handle)
{
    return static_cast<const Font*>(handle)->charset;
}

static void deleteFont(FPDF_SYSFONTINFO*, void* handle)
{
    delete static_cast<Font*>(handle);
}

// Nothing to enumerate: every request is matched as it comes.
static void enumFonts(FPDF_SYSFONTINFO*, void*)
{
}

static void release(FPDF_SYSFONTINFO*)
{
}

FPDF_SYSFONTINFO* systemFonts()
{
    static FPDF_SYSFONTINFO info = {1, release, enumFonts, mapFont, getFont, getFontData, getFaceName,
                                    getFontCharset, deleteFont};
    return &info;
}

} // namespace cutefish::pdf
