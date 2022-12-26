add_subdirectory(fmtlib)

add_subdirectory(llvm-project/llvm)
set(EXTERNAL_LLVM_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/llvm-project/llvm/include
        ${PROJECT_BINARY_DIR}/external/llvm-project/llvm/include)
set_property(GLOBAL PROPERTY EXTERNAL_LLVM_INCLUDE_DIRECTORIES "${EXTERNAL_LLVM_INCLUDE_DIRECTORIES}")

add_subdirectory(taskflow)
add_subdirectory(mio)
