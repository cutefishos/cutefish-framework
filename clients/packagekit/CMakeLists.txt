cutefish_add_client(packagekit TARGET PackageKitClient
    SOURCES installer.cpp transaction.cpp updates.cpp
    HEADERS installer.h transaction.h updates.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
