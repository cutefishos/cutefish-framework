set(CUTEFISH_APPEARANCE_SOURCES
    wallpaper.cpp
    wallpaper.h
    plugin.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick DBus)

add_library(cutefish-framework-appearance SHARED ${CUTEFISH_APPEARANCE_SOURCES})

qt_add_qml_module(cutefish-framework-appearance
    URI Cutefish.Appearance
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-appearance
    CLASS_NAME QmlPlugins
    NO_GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Appearance"
)

add_library(Cutefish::Appearance ALIAS cutefish-framework-appearance)

target_link_libraries(cutefish-framework-appearance PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    Qt6::DBus
)

cutefish_install_qml_module(cutefish-framework-appearance)
