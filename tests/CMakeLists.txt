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
