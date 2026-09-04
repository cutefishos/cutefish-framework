find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)

set(CUTEFISH_NETWORK_SOURCES
    appletproxymodel.cpp
    appletproxymodel.h

    activeconnection.cpp
    activeconnection.h

    networkitemslist.cpp
    networkitemslist.h

    networkmodel.cpp
    networkmodel.h

    networkmodelitem.cpp
    networkmodelitem.h

    wirelessitemsettings.cpp
    wirelessitemsettings.h

    uiutils.cpp
    uiutils.h

    identitymodel.cpp
    identitymodel.h

    handler.cpp
    handler.h

    configuration.cpp
    configuration.h

    enabledconnections.cpp
    enabledconnections.h

    enums.cpp
    enums.h

    wifisettings.cpp
    wifisettings.h

    qmlplugins.cpp
    qmlplugins.h
)

find_package(KF6NetworkManagerQt REQUIRED)
find_package(KF6ModemManagerQt REQUIRED)

add_library(cutefish-framework-network SHARED ${CUTEFISH_NETWORK_SOURCES})
generate_export_header(cutefish-framework-network EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/network_export.h BASE_NAME Cutefish_Network)

qt_add_qml_module(cutefish-framework-network
    URI Cutefish.Network
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-network
    CLASS_NAME QmlPlugins
    NO_GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Network"
)

add_library(Cutefish::Network ALIAS cutefish-framework-network)

target_link_libraries(cutefish-framework-network PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    Qt6::DBus

    KF6::NetworkManagerQt
    KF6::ModemManagerQt
)

cutefish_install_qml_module(cutefish-framework-network)
