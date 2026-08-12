cmake_minimum_required(VERSION 3.21)

foreach(required DOTNET_EXECUTABLE MODFORGE_CLI_PROJECT CONTRACT_TEST_EXECUTABLE WORK_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "catalog contract runner needs -D${required}=...")
    endif()
endforeach()
if(NOT EXISTS "${MODFORGE_CLI_PROJECT}")
    message(FATAL_ERROR "sibling ModForge CLI project not found: ${MODFORGE_CLI_PROJECT}")
endif()
if(NOT EXISTS "${CONTRACT_TEST_EXECUTABLE}")
    message(FATAL_ERROR "catalog contract consumer executable not found: ${CONTRACT_TEST_EXECUTABLE}")
endif()
cmake_path(GET WORK_DIR FILENAME work_leaf)
if(NOT work_leaf STREQUAL "modforge-catalog-contract")
    message(FATAL_ERROR
        "refusing to replace a contract work directory without the expected leaf name: ${WORK_DIR}")
endif()

# WORK_DIR is always supplied from CMAKE_CURRENT_BINARY_DIR by tests/CMakeLists.txt.
# Keep the producer artifacts after the test for inspection; the next run replaces
# only this dedicated directory.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(full_spec "${WORK_DIR}/ContractFull.json")
set(light_spec "${WORK_DIR}/ContractLight.json")
set(full_plugin "${WORK_DIR}/ContractFull.esp")
set(light_plugin "${WORK_DIR}/ContractLight.esl")
set(database "${WORK_DIR}/catalog.db")
set(catalog "${WORK_DIR}/scene-catalog.json")

file(WRITE "${full_spec}" [=[{
  "pluginName": "ContractFull.esp",
  "esl": false,
  "miscItems": [{
    "editorId": "MF_ContractFull",
    "name": "Full catalog metadata",
    "model": "Clutter\\ContractFull.nif"
  }]
}
]=])
file(WRITE "${light_spec}" [=[{
  "pluginName": "ContractLight.esl",
  "esl": true,
  "miscItems": [{
    "editorId": "MF_ContractLight",
    "name": "Light catalog metadata",
    "model": "Clutter\\ContractLight.nif"
  }]
}
]=])

execute_process(
    COMMAND "${DOTNET_EXECUTABLE}" build "${MODFORGE_CLI_PROJECT}" --configuration Release --nologo
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "ModForge build failed (${build_result})\n${build_stdout}\n${build_stderr}")
endif()

function(run_modforge)
    execute_process(
        COMMAND "${DOTNET_EXECUTABLE}" run --project "${MODFORGE_CLI_PROJECT}"
            --configuration Release --no-build --no-restore -- ${ARGN}
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr
    )
    if(NOT command_result EQUAL 0)
        string(JOIN " " rendered_command ${ARGN})
        message(FATAL_ERROR
            "ModForge '${rendered_command}' failed (${command_result})\n${command_stdout}\n${command_stderr}")
    endif()
endfunction()

run_modforge(build "${full_spec}" "${full_plugin}")
run_modforge(build "${light_spec}" "${light_plugin}")
run_modforge(catalog build "${database}" "${full_plugin}" "${light_plugin}")
run_modforge(catalog export-json "${database}" "${catalog}")

execute_process(
    COMMAND "${CONTRACT_TEST_EXECUTABLE}" "${catalog}" "${full_plugin}" "${light_plugin}"
    RESULT_VARIABLE consumer_result
    OUTPUT_VARIABLE consumer_stdout
    ERROR_VARIABLE consumer_stderr
)
if(NOT consumer_result EQUAL 0)
    message(FATAL_ERROR
        "CatalogFile rejected the live ModForge export (${consumer_result})\n${consumer_stdout}\n${consumer_stderr}")
endif()
message(STATUS "${consumer_stdout}")
