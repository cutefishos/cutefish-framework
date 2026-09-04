find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)

set(NETWORKMGR_SRCS
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

    wiressitemsettings.cpp
    wiressitemsettings.h

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

add_library(cutefishnm_qmlplugins SHARED ${NETWORKMGR_SRCS})
generate_export_header(cutefishnm_qmlplugins EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/networkmanager_export.h BASE_NAME NetworkManager)

target_link_libraries (cutefishnm_qmlplugins
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    Qt6::DBus

    KF6::NetworkManagerQt
    KF6::ModemManagerQt
)

install(TARGETS cutefishnm_qmlplugins DESTINATION ${INSTALL_QMLDIR}/Cutefish/NetworkManagement)
install(FILES qmldir DESTINATION ${INSTALL_QMLDIR}/Cutefish/NetworkManagement)
