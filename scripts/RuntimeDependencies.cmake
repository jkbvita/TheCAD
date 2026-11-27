# Resolve only imported Release runtime libraries; never copy an SDK tree.
if(NOT APP OR NOT LIBRARY_BIN OR NOT OUTPUT_FILE OR NOT DUMPBIN)
    message(FATAL_ERROR "APP, LIBRARY_BIN, OUTPUT_FILE and DUMPBIN are required")
endif()
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "dumpbin")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${DUMPBIN}")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${APP}"
    LIBRARIES ${EXTRA_LIBRARIES}
    DIRECTORIES "${LIBRARY_BIN}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]([Ss][Yy][Ss][Tt][Ee][Mm]32|[Ww][Ii][Nn][Ss][Xx][Ss])[/\\\\].*")
if(unresolved)
    message(FATAL_ERROR "Unresolved runtime libraries: ${unresolved}")
endif()
if(conflicts_FILENAMES)
    message(FATAL_ERROR "Conflicting runtime library paths: ${conflicts_FILENAMES}")
endif()
list(JOIN resolved "\n" contents)
file(WRITE "${OUTPUT_FILE}" "${contents}\n")
