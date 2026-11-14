cutefish_add_client(packagekit TARGET PackageKitClient
    SOURCES transaction.cpp updates.cpp
    HEADERS transaction.h updates.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
