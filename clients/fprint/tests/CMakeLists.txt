find_package(GTest REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
include(GoogleTest)

add_executable(fprint-client-tests fprint_test.cpp)
target_link_libraries(fprint-client-tests PRIVATE Cutefish::FprintClient PkgConfig::SYSTEMD GTest::gtest_main)
# Each case owns net.reactivated.Fprint on the shared session bus.
gtest_discover_tests(fprint-client-tests PROPERTIES RESOURCE_LOCK fprint-session-bus TIMEOUT 60)
