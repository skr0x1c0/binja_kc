cmake_minimum_required(VERSION 3.22)

include(CheckCXXCompilerFlag)

project(binja_kc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-Wall
        -Werror=return-type
        -Werror=switch
        -Wno-deprecated-declarations
        -Wno-unused-local-typedef
        -Wno-deprecated-enum-enum-conversion
        -Wno-deprecated-anon-enum-enum-conversion)
check_cxx_compiler_flag(-Wdeprecated-builtins COMPILER_HAS_DEPRECATED_BUILTINS)
if (${COMPILER_HAS_DEPRECATED_BUILTINS})
    add_compile_options(-Wno-deprecated-builtins)
endif ()

add_library(${PROJECT_NAME} SHARED main.cpp)

find_path(
        BN_API_PATH
        NAMES binaryninjaapi.h
        HINTS $ENV{BN_API_PATH}
        REQUIRED)
add_subdirectory(${BN_API_PATH} api)

add_subdirectory(external)
add_subdirectory(common)
add_subdirectory(debuginfo)
add_subdirectory(kcview)

target_link_libraries(${PROJECT_NAME} PRIVATE dwarf_debuginfo kcview)

set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        CXX_STANDARD_REQUIRED ON
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/bin)

get_target_property(BN_API_SOURCE_DIR binaryninjaapi SOURCE_DIR)
list(APPEND CMAKE_MODULE_PATH "${BN_API_SOURCE_DIR}/cmake")
find_package(BinaryNinjaCore REQUIRED)
add_custom_target(install_binja_kc
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:binja_kc> ${BinaryNinjaCore_USER_PLUGINS_DIR}
        DEPENDS binja_kc
        COMMENT "Installing binja_kc to ${BinaryNinjaCore_USER_PLUGINS_DIR}")
