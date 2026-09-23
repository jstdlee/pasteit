target_sources(pastit_core PRIVATE
    src/app/generated_filename.cpp
    src/app/file_operation_confirmation.cpp)

if (PASTIT_BUILD_TESTS)
    add_executable(generated_filename_test tests/generated_filename_test.cpp)
    target_link_libraries(generated_filename_test PRIVATE pastit_core)
    add_test(NAME generated_filename_test COMMAND generated_filename_test)

    add_executable(file_operation_confirmation_test tests/file_operation_confirmation_test.cpp)
    target_link_libraries(file_operation_confirmation_test PRIVATE pastit_core)
    add_test(NAME file_operation_confirmation_test COMMAND file_operation_confirmation_test)
endif()
