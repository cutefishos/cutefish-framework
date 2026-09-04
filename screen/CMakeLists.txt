set(CUTEFISH_SCREEN_SOURCES
    output.cpp
    outputmodel.cpp
    plugin.cpp
    screen.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
find_package(KF6Screen REQUIRED)

add_library(cutefish-framework-screen SHARED ${CUTEFISH_SCREEN_SOURCES})

qt_add_qml_module(cutefish-framework-screen
    URI Cutefish.Screen
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-screen
    CLASS_NAME QmlPlugins
    NO_GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Screen"
)

add_library(Cutefish::Screen ALIAS cutefish-framework-screen)

target_link_libraries(cutefish-framework-screen PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    KF6::Screen
)

cutefish_install_qml_module(cutefish-framework-screen)
