cmake_minimum_required(VERSION 3.5)
project(cutefish-screen LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(SCREEN_SRCS
    qt5/output.cpp
    qt5/outputmodel.cpp
    qt5/plugin.cpp
    qt5/screen.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)

add_library(cutefishscreen_qmlplugins SHARED ${SCREEN_SRCS})

target_link_libraries (cutefishscreen_qmlplugins
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
)

install(TARGETS cutefishscreen_qmlplugins DESTINATION ${INSTALL_QMLDIR}/Cutefish/Screen)
install(FILES qmldir DESTINATION ${INSTALL_QMLDIR}/Cutefish/Screen)
