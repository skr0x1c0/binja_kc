add_executable(kcview_test main.cpp)

find_package(LLVM 15.0 REQUIRED
        COMPONENTS support object debugInfoDWARF)
target_link_directories(kcview_test PRIVATE ${LLVM_LIBRARY_DIRS})
target_link_libraries(kcview_test PRIVATE ${LLVM_LIBRARIES} libzstd_static)
target_link_options(kcview_test PRIVATE -lz -lm -lcurses)

target_link_libraries(kcview_test PRIVATE kcview)
