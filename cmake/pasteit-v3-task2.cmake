target_sources(pastit_core PRIVATE src/history/clipboard_history_store.cpp)

if (PASTIT_BUILD_TESTS)
    add_executable(clipboard_history_store_test tests/clipboard_history_store_test.cpp)
    target_link_libraries(clipboard_history_store_test PRIVATE pastit_core)
    add_test(NAME clipboard_history_store_test COMMAND clipboard_history_store_test)

    add_executable(clipboard_orphan_cleanup_test tests/clipboard_orphan_cleanup_test.cpp)
    target_link_libraries(clipboard_orphan_cleanup_test PRIVATE pastit_core)
    add_test(NAME clipboard_orphan_cleanup_test COMMAND clipboard_orphan_cleanup_test)
endif()
