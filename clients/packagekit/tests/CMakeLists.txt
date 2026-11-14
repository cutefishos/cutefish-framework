find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(packagekit-client-tests updates_test.cpp)
target_link_libraries(packagekit-client-tests PRIVATE Cutefish::PackageKitClient GTest::gtest_main)
# On a session bus of its own without service files: some desktops install a
# session service under PackageKit's name, which would answer the absent
# daemon's case.
if(CMAKE_VERSION VERSION_LESS 3.29)
    message(FATAL_ERROR "packagekit-client-tests need CMake 3.29 for TEST_LAUNCHER")
endif()
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
set_target_properties(packagekit-client-tests PROPERTIES TEST_LAUNCHER
    "${DBUS_RUN_SESSION_EXECUTABLE};--config-file=${CMAKE_CURRENT_SOURCE_DIR}/dbus-session.conf;--")
gtest_discover_tests(packagekit-client-tests)
