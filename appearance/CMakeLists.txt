set(CUTEFISH_APPEARANCE_SOURCES
    appearance.cpp
    appearance.h
    wallpaper.cpp
    wallpaper.h
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml DBus)

include(GNUInstallDirs)
if(NOT COMMAND cutefish_install_qml_module)
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/CutefishFrameworkQml.cmake")
endif()

add_library(cutefish-framework-appearance SHARED ${CUTEFISH_APPEARANCE_SOURCES})
add_library(Cutefish::Appearance ALIAS cutefish-framework-appearance)

target_include_directories(cutefish-framework-appearance
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cutefish>
)

target_link_libraries(cutefish-framework-appearance PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
)

# Consumers pull this directory in with add_subdirectory to link the library;
# only the framework's own build ships it and the QML module, so their packages
# do not fight over the same files.
if(PROJECT_NAME STREQUAL "CutefishFramework")
    install(TARGETS cutefish-framework-appearance
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(FILES appearance.h wallpaper.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cutefish)

    # qmltyperegistrar resolves the QML_FOREIGN types out of this file.
    qt_extract_metatypes(cutefish-framework-appearance)

    # The QML module is a plugin of its own so the module registration ships in
    # exactly one library, even in a process that also links the one above.
    qt_add_qml_module(cutefish-framework-appearance-qml
        URI Cutefish.Appearance
        VERSION 1.0
        PLUGIN_TARGET cutefish-framework-appearance-qml
        CLASS_NAME AppearanceQmlPlugin
        NO_PLUGIN_OPTIONAL
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Appearance"
        SOURCES
            qmlregistration.cpp
            qmlregistration.h
    )

    target_link_libraries(cutefish-framework-appearance-qml PRIVATE
        Cutefish::Appearance
        Qt6::Qml
    )

    cutefish_install_qml_module(cutefish-framework-appearance-qml)
endif()
