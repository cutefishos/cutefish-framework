find_package(Qt6 REQUIRED COMPONENTS Core DBus Qml)

set(CUTEFISH_BLUETOOTH_SOURCES
    declarativemanager.cpp
    declarativeadapter.cpp
    # declarativebattery.cpp
    declarativedevice.cpp
    declarativeinput.cpp
    declarativemediaplayer.cpp
    declarativedevicesmodel.cpp
    bluezqtextensionplugin.cpp
    applet/devicesproxymodel.cpp
    applet/bluetoothagent.cpp
    applet/bluetoothmanager.cpp
)

add_library(cutefish-framework-bluetooth SHARED ${CUTEFISH_BLUETOOTH_SOURCES})

find_package(KF6BluezQt REQUIRED)

qt_add_qml_module(cutefish-framework-bluetooth
    URI Cutefish.Bluetooth
    VERSION 1.0
    PLUGIN_TARGET cutefish-framework-bluetooth
    CLASS_NAME BluezQtExtensionPlugin
    NO_GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Cutefish/Bluetooth"
    QML_FILES DevicesModel.qml
)

add_library(Cutefish::Bluetooth ALIAS cutefish-framework-bluetooth)

target_link_libraries(cutefish-framework-bluetooth PUBLIC
    Qt6::Core
    Qt6::Qml
    Qt6::DBus
    KF6::BluezQt
)

cutefish_install_qml_module(cutefish-framework-bluetooth)
install(FILES DevicesModel.qml DESTINATION ${INSTALL_QMLDIR}/Cutefish/Bluetooth)
