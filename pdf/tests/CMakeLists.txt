find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
include(GoogleTest)

add_executable(pdf-tests pdf_test.cpp)
target_link_libraries(pdf-tests PRIVATE Cutefish::Pdf GTest::gtest_main Threads::Threads)
target_compile_definitions(pdf-tests PRIVATE PDF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
gtest_discover_tests(pdf-tests)
