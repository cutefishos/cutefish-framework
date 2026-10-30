cutefish_add_client(accounts TARGET AccountsClient
    SOURCES account_picture.cpp accounts_manager.cpp user_account.cpp
    HEADERS account_picture.h accounts_manager.h user_account.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
