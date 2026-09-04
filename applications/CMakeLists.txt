find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Qml)

set(CUTEFISH_APPLICATIONS_SRCS
    applicationlauncher.cpp
    applicationlauncher.h
    applicationregistry.cpp
    applicationregistry.h
    desktopentry.cpp
    desktopentry.h
)

add_library(cutefishapplications STATIC ${CUTEFISH_APPLICATIONS_SRCS})
set_target_properties(cutefishapplications PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(cutefishapplications PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_include_directories(cutefishapplications PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutefishapplications
    PUBLIC
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
)
