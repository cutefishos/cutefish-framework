// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

struct _FPDF_SYSFONTINFO;

namespace cutefish::pdf {

// PDFium's source of fonts a document does not embed, answering each request
// with fontconfig's match for the name, weight, slant and the language of the
// character set, instead of PDFium's own walk through the font folders.
_FPDF_SYSFONTINFO* systemFonts();

} // namespace cutefish::pdf
