find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Qml Quick Widgets)

add_definitions(-DTRANSLATION_DOMAIN=\"cutefish_pulseaudio\")

set(USE_GSETTINGS False)
set(USE_GCONF False)

configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

set(CUTEFISH_AUDIO_SOURCES
    card.cpp
    client.cpp
    context.cpp
    device.cpp
    maps.cpp
    operation.cpp
    port.cpp
    profile.cpp
    pulseaudio.cpp
    pulseobject.cpp
    sink.cpp
    sinkinput.cpp
    modulemanager.cpp
    source.cpp
    sourceoutput.cpp
    stream.cpp
    volumemonitor.cpp
    volumeobject.cpp
    debug.cpp
    server.cpp
    streamrestore.cpp
    module.cpp
    canberracontext.cpp
    speakertest.cpp
    qml/listitemmenu.cpp
    qml/plugin.cpp
    # qml/microphoneindicator.cpp
    # qml/volumeosd.cpp
    qml/volumefeedback.cpp

    model/sortfiltermodel.cpp
)

set(CUTEFISH_AUDIO_QML_FILES
    PulseObjectFilterModel.qml
)

find_package(PkgConfig)
find_package(Canberra REQUIRED)

find_package(CanberraPulse)
set_package_properties(CanberraPulse PROPERTIES
    DESCRIPTION "Pulseaudio backend for libcanberra"
    PURPOSE "Required for volume feedback sounds"
    TYPE RUNTIME
)

find_package(SoundThemeFreedesktop)
set_package_properties(SoundThemeFreedesktop PROPERTIES
    DESCRIPTION "The standard freedesktop sound theme"
    PURPOSE "Required for volume feedback sounds"
    URL "https://www.freedesktop.org/wiki/Specifications/sound-theme-spec/"
    TYPE RUNTIME
)

pkg_check_modules(LIBPULSE libpulse REQUIRED IMPORTED_TARGET)
pkg_check_modules(LIBPULSE_MAINLOOP libpulse-mainloop-glib REQUIRED IMPORTED_TARGET)

add_library(cutefish-framework-audio SHARED ${CUTEFISH_AUDIO_SOURCES})

qt_add_qml_module(cutefish-framework-audio
    URI Cutefish.Audio
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-audio
    CLASS_NAME Plugin
    NO_GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Audio"
    QML_FILES ${CUTEFISH_AUDIO_QML_FILES}
)

add_library(Cutefish::Audio ALIAS cutefish-framework-audio)

target_link_libraries(cutefish-framework-audio PUBLIC
    Qt6::Core
    Qt6::Qml
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Quick
    Canberra::Canberra
    PkgConfig::LIBPULSE
    PkgConfig::LIBPULSE_MAINLOOP
)

cutefish_install_qml_module(cutefish-framework-audio)
install(FILES ${CUTEFISH_AUDIO_QML_FILES} DESTINATION ${INSTALL_QMLDIR}/Cutefish/Audio)
