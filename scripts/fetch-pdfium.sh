#!/usr/bin/env bash
# Fetches the pinned PDFium revision into third_party/pdfium, with only the
# folders the library is built from, and the few files of Chromium's build
# and of dragonbox and fast_float it includes into third_party/pdfium-deps.
# FreeType, lcms2, libjpeg, zlib, ICU, OpenJPEG and Abseil come from the
# system.
set -Eeuo pipefail

PDFIUM_URL=https://pdfium.googlesource.com/pdfium
PDFIUM_BRANCH=chromium/8158
PDFIUM_REVISION=99da130a26ad610843d041a509efd8ea68cc05b7
# The revisions PDFium's DEPS names at PDFIUM_REVISION.
CHROMIUM_GIT=https://chromium.googlesource.com
BUILD_REVISION=0c62137b8623a3fab2c6aa5d7c94f719b3aa31f6
DRAGONBOX_REVISION=beeeef91cf6fef89a4d4ba5e95d47ca64ccb3a44
FAST_FLOAT_REVISION=42f254836ee416ffcf2f4e6576260003ac33d48c

mkdir -p "$(dirname "$0")/../third_party"
cd "$(dirname "$0")/../third_party"

if [[ ! -d pdfium/.git ]]; then
  git clone --quiet --filter=blob:none --no-checkout --depth 1 --branch "$PDFIUM_BRANCH" "$PDFIUM_URL" pdfium
fi
(
  cd pdfium
  if [[ "$(git rev-parse --verify --quiet HEAD)" != "$PDFIUM_REVISION" ]]; then
    git fetch --quiet --filter=blob:none --depth 1 origin "$PDFIUM_REVISION"
  fi
  git sparse-checkout set --cone constants core fpdfsdk fxjs public \
      third_party/agg23 third_party/bigint third_party/freetype
  git checkout --quiet --detach "$PDFIUM_REVISION"
)

# A file of a gitiles repository at a revision.
fetch_file() {
  mkdir -p "$(dirname "$3")"
  curl --fail --silent --show-error "$CHROMIUM_GIT/$1/+/$2/$4?format=TEXT" | base64 -d > "$3"
}

# A folder of a gitiles repository at a revision.
fetch_folder() {
  mkdir -p "$3"
  curl --fail --silent --show-error "$CHROMIUM_GIT/$1/+archive/$2/$4.tar.gz" | tar xzf - -C "$3"
}

deps=pdfium-deps
stamp="$deps/.revision"
if [[ "$(cat "$stamp" 2>/dev/null)" != "$PDFIUM_REVISION" ]]; then
  rm -rf "$deps"
  for file in build_config.h buildflag.h; do
    fetch_file chromium/src/build "$BUILD_REVISION" "$deps/build/$file" "$file"
  done
  fetch_folder external/github.com/jk-jeon/dragonbox "$DRAGONBOX_REVISION" "$deps/dragonbox/include" include
  fetch_folder external/github.com/fastfloat/fast_float "$FAST_FLOAT_REVISION" "$deps/fast_float/include" include
  echo "$PDFIUM_REVISION" > "$stamp"
fi
