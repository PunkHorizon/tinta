if(NOT DEFINED TEST_BINARY OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "TEST_BINARY and TEST_DIR are required")
endif()
# A fresh folder each run, as on CI: files a previous run saved must not
# stand in for ones this run creates
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
configure_file("${TEST_BINARY}" "${TEST_DIR}/input_tests.exe" COPYONLY)
file(WRITE "${TEST_DIR}/settings.ini" "; Isolated superscript test configuration\n")
execute_process(COMMAND "${TEST_DIR}/input_tests.exe" --superscript-tests
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native superscript test failed: ${result}")
endif()
