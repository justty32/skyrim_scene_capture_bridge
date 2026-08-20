cmake_minimum_required(VERSION 3.21)
project(SceneCaptureBridgePortableTests LANGUAGES CXX)

include(CTest)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(catalog_file_tests
    CatalogFileTests.cpp
    ../src/CatalogFile.cpp
)
target_compile_features(catalog_file_tests PRIVATE cxx_std_23)
target_include_directories(catalog_file_tests PRIVATE ../src)
target_link_libraries(catalog_file_tests PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(catalog_file_tests PRIVATE /W4 /WX)
else()
    target_compile_options(catalog_file_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME catalog_file_tests COMMAND catalog_file_tests)

add_executable(catalog_compatibility_probe
    CatalogCompatibilityProbe.cpp
    ../src/CatalogFile.cpp
)
target_compile_features(catalog_compatibility_probe PRIVATE cxx_std_23)
target_include_directories(catalog_compatibility_probe PRIVATE ../src)
target_link_libraries(catalog_compatibility_probe PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(catalog_compatibility_probe PRIVATE /W4 /WX)
else()
    target_compile_options(catalog_compatibility_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Live producer/consumer contract: when the sibling ModForge checkout and dotnet
# are available, CTest asks the real CLI to build synthetic full/light plugins,
# export scene-catalog.json, then feeds those bytes to CatalogFile.
set(MODFORGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../ModForge" CACHE PATH
    "Sibling ModForge checkout used by the live catalog contract test")
find_program(DOTNET_EXECUTABLE NAMES dotnet)
set(MODFORGE_CLI_PROJECT "${MODFORGE_ROOT}/src/ModForge.Cli/ModForge.Cli.csproj")
if(DOTNET_EXECUTABLE AND EXISTS "${MODFORGE_CLI_PROJECT}")
    add_executable(modforge_catalog_contract_tests
        CatalogContractTests.cpp
        ../src/CatalogFile.cpp
    )
    target_compile_features(modforge_catalog_contract_tests PRIVATE cxx_std_23)
    target_include_directories(modforge_catalog_contract_tests PRIVATE ../src)
    target_link_libraries(modforge_catalog_contract_tests PRIVATE nlohmann_json::nlohmann_json)
    if(MSVC)
        target_compile_options(modforge_catalog_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(modforge_catalog_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_test(NAME modforge_catalog_contract
        COMMAND "${CMAKE_COMMAND}"
            "-DDOTNET_EXECUTABLE=${DOTNET_EXECUTABLE}"
            "-DMODFORGE_CLI_PROJECT=${MODFORGE_CLI_PROJECT}"
            "-DCONTRACT_TEST_EXECUTABLE=$<TARGET_FILE:modforge_catalog_contract_tests>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/modforge-catalog-contract"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunModForgeCatalogContract.cmake"
    )
    set_tests_properties(modforge_catalog_contract PROPERTIES
        LABELS "integration;contract"
        TIMEOUT 240
    )
else()
    message(STATUS
        "ModForge catalog contract test disabled: dotnet or sibling ModForge checkout not found")
endif()
