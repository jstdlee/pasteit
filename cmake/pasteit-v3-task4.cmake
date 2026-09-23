target_sources(pastit_core PRIVATE src/history/path_target_resolver.cpp)

if (PASTIT_BUILD_TESTS)
    add_executable(path_target_resolver_test tests/path_target_resolver_test.cpp)
    target_link_libraries(path_target_resolver_test PRIVATE pastit_core)
    add_test(NAME path_target_resolver_test COMMAND path_target_resolver_test)
endif()
