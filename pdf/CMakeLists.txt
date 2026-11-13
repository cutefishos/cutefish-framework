# PDF: documents read with PDFium, built in as hidden objects; only the
# cutefish::pdf API is exported. scripts/fetch-pdfium.sh puts the pinned
# sources in third_party.
#   find_package(CutefishPdf REQUIRED)
#   target_link_libraries(app PRIVATE Cutefish::Pdf)

enable_language(C)

find_package(CutefishToolkit REQUIRED)
find_package(PkgConfig REQUIRED)

set(PDFIUM_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/pdfium)
set(PDFIUM_DEPS_DIR ${PROJECT_SOURCE_DIR}/third_party/pdfium-deps)
if(NOT EXISTS ${PDFIUM_SOURCE_DIR}/public/fpdfview.h OR NOT EXISTS ${PDFIUM_DEPS_DIR}/build/build_config.h)
    message(FATAL_ERROR "PDFium is missing; run scripts/fetch-pdfium.sh")
endif()

pkg_check_modules(PdfiumSystem REQUIRED IMPORTED_TARGET
    freetype2 harfbuzz-subset lcms2 libopenjp2 libjpeg zlib icu-uc libhwy
    absl_flat_hash_set absl_inlined_vector absl_cleanup)

# PDFium includes its dependencies by their Chromium checkout paths; these
# point them at the system's copies and at the fetched headers.
set(PDFIUM_SHIM_DIR ${CMAKE_CURRENT_BINARY_DIR}/pdfium-shim)
pkg_get_variable(PDFIUM_ABSL_INCLUDE_DIR absl_base includedir)
pkg_get_variable(PDFIUM_HWY_INCLUDE_DIR libhwy includedir)
foreach(link
        "third_party/abseil-cpp/absl|${PDFIUM_ABSL_INCLUDE_DIR}/absl"
        "third_party/highway/src/hwy|${PDFIUM_HWY_INCLUDE_DIR}/hwy"
        "third_party/fast_float/src/include|${PDFIUM_DEPS_DIR}/fast_float/include"
        "third_party/dragonbox/src/include|${PDFIUM_DEPS_DIR}/dragonbox/include"
        "build|${PDFIUM_DEPS_DIR}/build")
    string(REPLACE "|" ";" link ${link})
    list(GET link 0 name)
    list(GET link 1 target)
    if(NOT EXISTS ${PDFIUM_SHIM_DIR}/${name})
        get_filename_component(parent ${PDFIUM_SHIM_DIR}/${name} DIRECTORY)
        file(MAKE_DIRECTORY ${parent})
        file(CREATE_LINK ${target} ${PDFIUM_SHIM_DIR}/${name} SYMBOLIC)
    endif()
endforeach()

include(pdfium-sources.cmake)
list(TRANSFORM PDFIUM_SOURCES PREPEND ${PDFIUM_SOURCE_DIR}/)
add_library(pdfium-objects OBJECT ${PDFIUM_SOURCES})
set_target_properties(pdfium-objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
target_include_directories(pdfium-objects PRIVATE ${PDFIUM_SOURCE_DIR} ${PDFIUM_SHIM_DIR})
target_compile_definitions(pdfium-objects PRIVATE
    PDF_ENABLE_FREETYPE
    PDF_USE_AGG
    USE_SYSTEM_LCMS2
    USE_SYSTEM_LIBJPEG
    USE_SYSTEM_LIBOPENJPEG2
    USE_SYSTEM_ZLIB
    USE_SYSTEM_ICUUC
    DEFINE_PS_TABLES_DATA
)
# Upstream code, built as upstream builds it: its warnings are not ours.
target_compile_options(pdfium-objects PRIVATE -w)
target_link_libraries(pdfium-objects PRIVATE PkgConfig::PdfiumSystem)

cutefish_framework_add_library(cutefish-pdf
    TARGET Pdf
    MODULE pdf
    EXPORT CutefishPdfTargets
    SOURCES
        document.cpp
        $<TARGET_OBJECTS:pdfium-objects>
    HEADERS document.h
    PUBLIC_LIBRARIES Cutefish::ToolkitBase
    PRIVATE_LIBRARIES PkgConfig::PdfiumSystem
)
target_include_directories(cutefish-pdf PRIVATE ${PDFIUM_SOURCE_DIR})
# Only the library's own symbols: PDFium's, its inline functions' and the
# system libraries' stay inside.
target_link_options(cutefish-pdf PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cutefish-pdf.map)
set_property(TARGET cutefish-pdf APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cutefish-pdf.map)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

cutefish_framework_export(CutefishPdf CutefishPdfTargets)
cutefish_framework_add_package(CutefishPdf)
