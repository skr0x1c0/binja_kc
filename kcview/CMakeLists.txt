set(LIBRARY_NAME kcview)

set(KERNCACHE_HEADERS
        include/binja/kcview/errors.h
        include/binja/kcview/lib.h
        include/binja/kcview/range.h)

set(KERNCACHE_SOURCES
        src/lib.cpp)

add_library(${LIBRARY_NAME} STATIC ${KERNCACHE_SOURCES} ${KERNCACHE_HEADERS})
target_include_directories(${LIBRARY_NAME} PUBLIC include)
target_include_directories(${LIBRARY_NAME} PRIVATE include/binja/kcview)
target_link_libraries(${LIBRARY_NAME} PUBLIC binja_kc_common)
target_link_libraries(${LIBRARY_NAME} PUBLIC Taskflow fmt::fmt binaryninjaapi)

add_subdirectory(test)