qt_policy(SET QTP0001 NEW)

if(NOT DEFINED INSTALL_QMLDIR)
    find_program(QT_PATHS_EXECUTABLE NAMES qtpaths6 REQUIRED)
    execute_process(COMMAND ${QT_PATHS_EXECUTABLE} -query QT_INSTALL_QML
        OUTPUT_VARIABLE INSTALL_QMLDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

function(cutefish_install_qml_module target)
    qt_query_qml_module("${target}"
        TARGET_PATH module_path
        QMLDIR module_qmldir
        TYPEINFO module_qmltypes
    )

    set(module_install_dir "${INSTALL_QMLDIR}/${module_path}")
    install(TARGETS "${target}" DESTINATION "${module_install_dir}")
    install(FILES "${module_qmldir}" DESTINATION "${module_install_dir}")

    if(module_qmltypes)
        install(FILES "${module_qmltypes}" DESTINATION "${module_install_dir}")
    endif()
endfunction()
