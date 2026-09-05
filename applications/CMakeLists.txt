find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Qml)

set(CUTEFISH_APPLICATIONS_SRCS
    applicationlauncher.cpp
    applicationlauncher.h
    applicationruntime.cpp
    applicationruntime.h
    applicationregistry.cpp
    applicationregistry.h
    desktopentry.cpp
    desktopentry.h
)

add_library(cutefish-framework-applications STATIC ${CUTEFISH_APPLICATIONS_SRCS})
add_library(Cutefish::Applications ALIAS cutefish-framework-applications)
set_target_properties(cutefish-framework-applications PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(cutefish-framework-applications PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutefish-framework-applications
    PUBLIC
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
)
