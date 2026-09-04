find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)

set(CUTEFISH_ACCOUNTS_SOURCES
    accountsmanager.cpp
    accountsmanager.h
    accountsmanager_p.h
    useraccount.cpp
    useraccount.h
    useraccount_p.h
    usersmodel.cpp
    usersmodel.h
    usersmodel_p.h
    qmlplugins.cpp
)

qt6_add_dbus_interface(CUTEFISH_ACCOUNTS_SOURCES
                       org.freedesktop.Accounts.User.xml
                       user_interface)
qt6_add_dbus_interface(CUTEFISH_ACCOUNTS_SOURCES
                       org.freedesktop.Accounts.xml
                       accounts_interface)

add_library(cutefish-framework-accounts SHARED ${CUTEFISH_ACCOUNTS_SOURCES})

qt_add_qml_module(cutefish-framework-accounts
    URI Cutefish.Accounts
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-accounts
    CLASS_NAME QmlPlugins
    NO_GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Accounts"
)

add_library(Cutefish::Accounts ALIAS cutefish-framework-accounts)

target_link_libraries(cutefish-framework-accounts PUBLIC
    Qt6::Core
    Qt6::DBus
    Qt6::Quick
    Qt6::Gui
)

cutefish_install_qml_module(cutefish-framework-accounts)
