target_sources(pastit_core PRIVATE
    src/ui/clipboard_history_model.cpp
    src/ui/clipboard_history_panel.cpp)

if (PASTIT_BUILD_TESTS)
    add_executable(clipboard_history_model_test tests/clipboard_history_model_test.cpp)
    target_link_libraries(clipboard_history_model_test PRIVATE pastit_core)
    add_test(NAME clipboard_history_model_test COMMAND clipboard_history_model_test)
endif()
