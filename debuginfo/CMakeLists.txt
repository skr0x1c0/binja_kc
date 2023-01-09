set(LIBRARY_NAME dwarf_debuginfo)

set(DWARF_LOADER_HEADERS
        include/binja/debuginfo/errors.h
        include/binja/debuginfo/debug.h
        include/binja/debuginfo/dwarf.h
        include/binja/debuginfo/dwarf_task.h
        include/binja/debuginfo/function.h
        include/binja/debuginfo/macho_task.h
        include/binja/debuginfo/name_index.h
        include/binja/debuginfo/plugin_dsym.h
        include/binja/debuginfo/plugin_macho.h
        include/binja/debuginfo/plugin_symtab.h
        include/binja/debuginfo/source_finder.h
        include/binja/debuginfo/slider.h
        include/binja/debuginfo/types.h
        include/binja/debuginfo/variable.h)

set(DWARF_LOADER_SOURCES
        src/dsym.cpp
        src/dwarf.cpp
        src/dwarf_task.cpp
        src/function.cpp
        src/macho_task.cpp
        src/name_index.cpp
        src/plugin_dsym.cpp
        src/plugin_macho.cpp
        src/plugin_symtab.cpp
        src/types.cpp
        src/slider.cpp
        src/source_finder.cpp
        src/variable.cpp)

add_library(${LIBRARY_NAME} STATIC ${DWARF_LOADER_SOURCES} ${DWARF_LOADER_HEADERS})
target_include_directories(${LIBRARY_NAME} PUBLIC include/binja)

target_link_libraries(${LIBRARY_NAME} PRIVATE LLVMSupport LLVMDebugInfoDWARF LLVMObject)
get_property(LLVM_INCLUDE_DIRECTORIES GLOBAL PROPERTY EXTERNAL_LLVM_INCLUDE_DIRECTORIES)
target_include_directories(${LIBRARY_NAME} PUBLIC ${LLVM_INCLUDE_DIRECTORIES})
target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${LIBRARY_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/binja/debuginfo)

target_link_libraries(${LIBRARY_NAME} PUBLIC binja_kc_common)
target_link_libraries(${LIBRARY_NAME} PUBLIC binaryninjaapi fmt::fmt mio::mio Taskflow)

add_subdirectory(test)