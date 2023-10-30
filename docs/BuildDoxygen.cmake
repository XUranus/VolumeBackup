macro(build_doxygen)

# check if doxyegen is installed
find_package(Doxygen)
if (DOXYGEN_FOUND)
    # set input and output files
    set(DOXYGEN_IN "${CMAKE_CURRENT_LIST_DIR}/docs/Doxyfile")
    set(DOXYGEN_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    message("CMAKE_CURRENT_LIST_DIR is ${CMAKE_CURRENT_LIST_DIR}")

    # request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUTPUT} @ONLY)

    # note the option ALL which allows to build the docs together with the application
    add_custom_target(docs ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
else (DOXYGEN_FOUND)
    message ("Doxygen need to be installed to generate the doxygen document")
endif (DOXYGEN_FOUND)

endmacro()