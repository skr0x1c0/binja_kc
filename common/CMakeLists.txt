set(LIBRARY_NAME binja_kc_common)

set(BINJA_KC_COMMON_HEADERS
        include/binja/utils/binary_view.h
        include/binja/macho/macho.h
        include/binja/types/errors.h
        include/binja/types/uuid.h
        include/binja/utils/debug.h
        include/binja/utils/demangle.h
        include/binja/utils/log.h
        include/binja/utils/settings.h
        include/binja/utils/span_reader.h
        include/binja/utils/strconv.h
        include/binja/utils/interval_map.h)

set(BINJA_KC_COMMON_SOURCES
        src/macho/macho.cpp
        src/utils/binary_view.cpp
        src/utils/demangle.cpp
        src/utils/settings.cpp
        src/utils/span_reader.cpp
        src/utils/strconv.cpp)

add_library(${LIBRARY_NAME} STATIC ${BINJA_KC_COMMON_HEADERS} ${BINJA_KC_COMMON_SOURCES})
target_include_directories(${LIBRARY_NAME} PUBLIC include)
target_include_directories(${LIBRARY_NAME} PRIVATE include/binja)

target_link_libraries(${LIBRARY_NAME} PRIVATE LLVMSupport LLVMObject)
get_property(LLVM_INCLUDE_DIRECTORIES GLOBAL PROPERTY EXTERNAL_LLVM_INCLUDE_DIRECTORIES)
target_include_directories(${LIBRARY_NAME} PUBLIC ${LLVM_INCLUDE_DIRECTORIES})

target_link_libraries(${LIBRARY_NAME} PUBLIC binaryninjaapi fmt::fmt)