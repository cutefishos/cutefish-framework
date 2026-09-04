find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)

set(CUTEFISH_MEDIA_SOURCES
    mpris.cpp
    mpris.h
    mpriscontroller_p.h
    mpriscontroller.cpp
    mpriscontroller.h
    mprismanager.cpp
    mprismanager.h
    mprisplayer.cpp
    mprisplayer.h
    mprisplayeradaptor.cpp
    mprisplayerinterface.cpp
    mprisqt.h
    mprisrootadaptor.cpp
    mprisrootinterface.cpp
    qmlplugins.cpp

    qtdbusextended/dbusextended.h
    qtdbusextended/dbusextendedabstractinterface.cpp
    qtdbusextended/dbusextendedabstractinterface.h
    qtdbusextended/dbusextendedpendingcallwatcher_p.h
    qtdbusextended/dbusextendedpendingcallwatcher.cpp
)

add_library(cutefish-framework-media SHARED ${CUTEFISH_MEDIA_SOURCES})

qt_add_qml_module(cutefish-framework-media
    URI Cutefish.Media
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-media
    CLASS_NAME QmlPlugins
    NO_GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Media"
)

add_library(Cutefish::Media ALIAS cutefish-framework-media)

target_link_libraries(cutefish-framework-media PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    Qt6::DBus
)

cutefish_install_qml_module(cutefish-framework-media)
