set(CUTEFISH_SCREEN_SOURCES
    output.cpp
    output.h
    outputmodel.cpp
    outputmodel.h
    screen.cpp
    screen.h
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
find_package(KF6Screen REQUIRED)

add_library(cutefish-framework-screen SHARED ${CUTEFISH_SCREEN_SOURCES})
add_library(Cutefish::Screen ALIAS cutefish-framework-screen)

qt_add_qml_module(cutefish-framework-screen
    URI Cutefish.Screen
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-screen
    CLASS_NAME QmlPlugins
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Screen"
)

target_link_libraries(cutefish-framework-screen PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    KF6::Screen
)

cutefish_install_qml_module(cutefish-framework-screen)
