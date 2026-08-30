set(SCREEN_SRCS
    wallpaper.cpp
    wallpaper.h
    plugin.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick DBus)

add_library(cutefishsystem_qmlplugins SHARED ${SCREEN_SRCS})

target_link_libraries (cutefishsystem_qmlplugins
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    Qt6::DBus
)

install(TARGETS cutefishsystem_qmlplugins DESTINATION ${INSTALL_QMLDIR}/Cutefish/System)
install(FILES qmldir DESTINATION ${INSTALL_QMLDIR}/Cutefish/System)
