cmake_minimum_required(VERSION 3.20)

project(pastit LANGUAGES CXX)

option(PASTIT_BUILD_TESTS "Build PasteIt tests" ON)
option(PASTIT_BUILD_DESKTOP "Build the native Dear ImGui desktop shell when dependencies are available" ON)
option(PASTIT_FETCH_DESKTOP_DEPS "Fetch pinned GLFW, Dear ImGui, and stb sources like the prior ImGui demo" ON)

# A user-local sysroot keeps the desktop build reproducible on machines where
# development packages cannot be installed system-wide.
set(PASTIT_LOCAL_SYSROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/sysroot/usr")
if (EXISTS "${PASTIT_LOCAL_SYSROOT}")
    list(PREPEND CMAKE_INCLUDE_PATH "${PASTIT_LOCAL_SYSROOT}/include")
endif()

# The desktop already has versioned runtime libraries. Seed CMake with those
# exact files while taking headers from the private sysroot, matching the
# previous imgui_hf_demo build and avoiding a runtime dependency on .deps.
set(PASTIT_SYSTEM_LIBRARY_DIRS
    "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
    "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
function(pastit_seed_versioned_library variable filename)
    set(search_variable "PASTIT_${variable}_PATH")
    find_file(${search_variable} NAMES "${filename}" PATHS ${PASTIT_SYSTEM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    if (${search_variable})
        set(${variable} "${${search_variable}}" CACHE FILEPATH "PasteIt system runtime library" FORCE)
    endif()
endfunction()
if (NOT WIN32)
    pastit_seed_versioned_library(OPENGL_opengl_LIBRARY libOpenGL.so.0)
    pastit_seed_versioned_library(OPENGL_glx_LIBRARY libGLX.so.0)
    pastit_seed_versioned_library(OPENGL_gl_LIBRARY libGL.so.1)
    pastit_seed_versioned_library(X11_X11_LIB libX11.so.6)
    pastit_seed_versioned_library(X11_Xcursor_LIB libXcursor.so.1)
    pastit_seed_versioned_library(X11_Xext_LIB libXext.so.6)
    pastit_seed_versioned_library(X11_Xfixes_LIB libXfixes.so.3)
    pastit_seed_versioned_library(X11_Xi_LIB libXi.so.6)
    pastit_seed_versioned_library(X11_Xinerama_LIB libXinerama.so.1)
    pastit_seed_versioned_library(X11_Xrandr_LIB libXrandr.so.2)
    pastit_seed_versioned_library(X11_Xrender_LIB libXrender.so.1)
    find_package(CURL QUIET)
endif()
find_package(OpenGL QUIET)
if (NOT WIN32)
    find_package(X11 QUIET)
endif()

set(PASTIT_DESKTOP_SOURCES_READY FALSE)
set(PASTIT_DESKTOP_PLATFORM_READY FALSE)
if (WIN32 AND OpenGL_FOUND)
    set(PASTIT_DESKTOP_PLATFORM_READY TRUE)
elseif (OpenGL_FOUND AND X11_FOUND)
    set(PASTIT_DESKTOP_PLATFORM_READY TRUE)
endif()

if (PASTIT_BUILD_DESKTOP AND PASTIT_FETCH_DESKTOP_DEPS AND PASTIT_DESKTOP_PLATFORM_READY)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if (WIN32)
        set(GLFW_BUILD_WIN32 ON CACHE BOOL "" FORCE)
    else()
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    endif()
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE)
    FetchContent_Declare(imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b-docking.zip
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_Declare(stb_image
        URL https://raw.githubusercontent.com/nothings/stb/5736b15f7ea0ffb08dd38af21067c314d6a3aae9/stb_image.h
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_MakeAvailable(glfw imgui stb_image)

    add_library(pastit_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(pastit_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(pastit_imgui PUBLIC glfw OpenGL::GL)
    target_compile_features(pastit_imgui PUBLIC cxx_std_20)
    set(PASTIT_STB_IMAGE_INCLUDE_DIR "${stb_image_SOURCE_DIR}")
    set(PASTIT_DESKTOP_SOURCES_READY TRUE)
endif()

set(PASTIT_DESKTOP_MISSING "")
if (NOT OpenGL_FOUND)
    list(APPEND PASTIT_DESKTOP_MISSING "OpenGL")
endif()
if (NOT WIN32 AND NOT X11_FOUND)
    list(APPEND PASTIT_DESKTOP_MISSING "X11")
endif()
if (PASTIT_BUILD_DESKTOP AND NOT PASTIT_DESKTOP_SOURCES_READY)
    list(APPEND PASTIT_DESKTOP_MISSING "fetched GLFW/ImGui/stb sources")
endif()
list(JOIN PASTIT_DESKTOP_MISSING ", " PASTIT_DESKTOP_MISSING_TEXT)

set(PASTIT_PLATFORM_SOURCES
    src/platform/app_paths.cpp
)
if (WIN32)
    list(APPEND PASTIT_PLATFORM_SOURCES
        src/platform/windows/windows_strings.cpp
        src/platform/windows/windows_fast_action_services.cpp
        src/platform/windows/windows_desktop_services.cpp
        src/platform/windows/windows_single_instance.cpp
    )
else()
    list(APPEND PASTIT_PLATFORM_SOURCES
        src/platform/linux/linux_fast_action_services.cpp
        src/platform/linux/linux_x11_clipboard_impl.cpp
        src/platform/linux/linux_x11_focus_impl.cpp
        src/platform/linux/linux_desktop_services.cpp
        src/platform/linux/linux_recent_paths.cpp
        src/platform/linux/linux_single_instance.cpp
    )
endif()

add_library(pastit_core
    ${PASTIT_PLATFORM_SOURCES}
    src/ai/openai_compatible_client.cpp
    src/ai/mermaid_prompt.cpp
    src/ai/model_catalog.cpp
    src/ai/prompt_expander.cpp
    src/app/ai_result_state.cpp
    src/app/contact_result_state.cpp
    src/app/date_time_result_state.cpp
    src/app/download_job.cpp
    src/app/execution_feedback.cpp
    src/app/hash_result_state.cpp
    src/app/network_report_state.cpp
    src/app/renderer_result_state.cpp
    src/app/generated_filename.cpp
    src/app/file_operation_confirmation.cpp
    src/actions/action_catalog.cpp
    src/annotation/annotation_export.cpp
    src/annotation/image_annotation.cpp
    src/config/app_settings.cpp
    src/config/settings_store.cpp
    src/config/prompt_template_service.cpp
    src/decision/action_category.cpp
    src/decision/action_preference.cpp
    src/decision/candidate_selector.cpp
    src/decision/fallback_ranker.cpp
    src/decision/usage_model.cpp
    src/graph/graph_data.cpp
    src/graph/graph_renderer.cpp
    src/graph/chart.cpp
    src/table/table_data.cpp
    src/pipeline/pipeline.cpp
    src/pipeline/process_runner.cpp
    src/privacy/anonymizer.cpp
    src/privacy/name_lists.cpp
    src/transform/text_transforms.cpp
    src/core/core.cpp
    src/detect/content_detector.cpp
    src/detect/content_profile.cpp
    src/detect/fast_content_detector.cpp
    src/detect/resume_detector.cpp
    src/djev/decision_session.cpp
    src/djev/decision_ranker.cpp
    src/djev/djev_client.cpp
    src/executor/action_executor.cpp
    src/executor/email_executor.cpp
    src/executor/fast_action_executor.cpp
    src/executor/file_executor.cpp
    src/executor/url_executor.cpp
    src/executor/utility_executor.cpp
    src/history/clipboard_history_store.cpp
    src/history/path_history_store.cpp
    src/history/path_target_resolver.cpp
    src/history/usage_store.cpp
    src/net/http_client.cpp
    src/render/external_renderer.cpp
    src/render/markdown.cpp
    third_party/qrcodegen/qrcodegen.cpp
    src/storage/clipboard_store.cpp
    src/storage/path_history.cpp
    src/ui/desktop_app.cpp
    src/ui/desktop_flow.cpp
    src/ui/desktop_runtime.cpp
    src/ui/main_popup_panel.cpp
    src/ui/recent_paths_panel.cpp
    src/ui/settings_panel.cpp
    src/ui/prompt_templates_panel.cpp
    src/ui/image_preview_panel.cpp
    src/ui/contact_result_panel.cpp
    src/ui/network_report_panel.cpp
    src/ui/mermaid_preview_panel.cpp
    src/ui/qr_preview_panel.cpp
    src/ui/download_progress_panel.cpp
    src/ui/file_operation_confirmation_panel.cpp
    src/ui/hash_result_panel.cpp
    src/ui/image_annotation_panel.cpp
    src/ui/ai_result_panel.cpp
    src/ui/imgui_widgets.cpp
    src/ui/theme.cpp
    src/ui/data_views.cpp
    src/ui/pipeline_view.cpp
    src/ui/privacy_view.cpp
    src/ui/localization.cpp
    src/ui/clipboard_history_model.cpp
    src/ui/clipboard_history_panel.cpp
    src/ui/recent_paths_model.cpp
    src/ui/settings_model.cpp
    src/ui/popup.cpp
    src/ui/multi_viewport.cpp
    src/ui/provider_test_request.cpp
    src/util/json.cpp
    src/util/replace_file.cpp
)

find_package(ZLIB QUIET)
if (ZLIB_FOUND)
    target_link_libraries(pastit_core PUBLIC ZLIB::ZLIB)
    target_compile_definitions(pastit_core PUBLIC PASTIT_HAS_ZLIB=1)
else()
    message(STATUS "PasteIt PNG validation will use a lightweight fallback; zlib was not found.")
endif()

target_include_directories(pastit_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(pastit_core PUBLIC cxx_std_20)
target_include_directories(pastit_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qrcodegen
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)

if (CMAKE_DL_LIBS)
    target_link_libraries(pastit_core PUBLIC ${CMAKE_DL_LIBS})
endif()

if (CURL_FOUND AND NOT WIN32)
    target_link_libraries(pastit_core PUBLIC CURL::libcurl)
    target_compile_definitions(pastit_core PUBLIC PASTIT_HAS_CURL=1)
endif()

target_compile_definitions(pastit_core PUBLIC PASTIT_DESKTOP_MISSING="${PASTIT_DESKTOP_MISSING_TEXT}")

if (WIN32)
    target_compile_definitions(pastit_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(pastit_core PUBLIC user32 shell32 ole32 uuid gdi32 bcrypt winhttp)
endif()

if (NOT WIN32 AND X11_FOUND)
    target_include_directories(pastit_core SYSTEM PUBLIC ${X11_INCLUDE_DIR})
    target_link_libraries(pastit_core PUBLIC ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
    target_compile_definitions(pastit_core PUBLIC PASTIT_HAS_X11=1)
endif()

if (PASTIT_BUILD_DESKTOP AND PASTIT_DESKTOP_PLATFORM_READY AND PASTIT_DESKTOP_SOURCES_READY)
    target_sources(pastit_core PRIVATE src/ui/stb_image_impl.cpp)
    target_include_directories(pastit_core SYSTEM PUBLIC ${PASTIT_STB_IMAGE_INCLUDE_DIR})
    target_link_libraries(pastit_core PUBLIC pastit_imgui)
    target_compile_definitions(pastit_core PUBLIC PASTIT_HAS_DESKTOP_DEPS=1)
elseif(PASTIT_BUILD_DESKTOP)
    message(STATUS "PasteIt desktop shell built as headless fallback; missing: ${PASTIT_DESKTOP_MISSING_TEXT}")
endif()

if (MSVC)
    target_compile_options(pastit_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(pastit_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pastit src/main.cpp)
target_link_libraries(pastit PRIVATE pastit_core)
target_compile_features(pastit PRIVATE cxx_std_20)
install(TARGETS pastit RUNTIME DESTINATION bin)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/mermaid.min.js
               ${CMAKE_CURRENT_BINARY_DIR}/mermaid.min.js COPYONLY)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/mermaid.min.js DESTINATION bin)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/lucide.ttf
               ${CMAKE_CURRENT_BINARY_DIR}/lucide.ttf COPYONLY)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/lucide.ttf DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/MERMAID_LICENSE DESTINATION bin)

if (PASTIT_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(core_contracts_test tests/core_contracts_test.cpp)
    target_link_libraries(core_contracts_test PRIVATE pastit_core)
    add_test(NAME core_contracts_test COMMAND core_contracts_test)

    add_executable(platform_contracts_test tests/platform_contracts_test.cpp)
    target_link_libraries(platform_contracts_test PRIVATE pastit_core)
    add_test(NAME platform_contracts_test COMMAND platform_contracts_test)

    if (NOT WIN32)
        add_executable(fast_action_services_test tests/fast_action_services_test.cpp)
        target_link_libraries(fast_action_services_test PRIVATE pastit_core)
        add_test(NAME fast_action_services_test COMMAND fast_action_services_test)
    endif()

    add_executable(settings_test tests/settings_test.cpp)
    target_link_libraries(settings_test PRIVATE pastit_core)
    add_test(NAME settings_test COMMAND settings_test)

    add_executable(prompt_template_test tests/prompt_template_test.cpp)
    target_link_libraries(prompt_template_test PRIVATE pastit_core)
    add_test(NAME prompt_template_test COMMAND prompt_template_test)

    add_executable(candidate_selector_test tests/candidate_selector_test.cpp)
    target_link_libraries(candidate_selector_test PRIVATE pastit_core)
    add_test(NAME candidate_selector_test COMMAND candidate_selector_test)
    add_executable(action_preference_test tests/action_preference_test.cpp)
    target_link_libraries(action_preference_test PRIVATE pastit_core)
    add_test(NAME action_preference_test COMMAND action_preference_test)

    add_executable(http_error_test tests/http_error_test.cpp)
    target_link_libraries(http_error_test PRIVATE pastit_core)
    add_test(NAME http_error_test COMMAND http_error_test)

    add_executable(prompt_action_test tests/prompt_action_test.cpp)
    target_link_libraries(prompt_action_test PRIVATE pastit_core)
    add_test(NAME prompt_action_test COMMAND prompt_action_test)

    foreach(task6_ui_test
            prompt_templates_panel_model_test
            prompt_action_visibility_test
            file_operation_confirmation_panel_test)
        add_executable(${task6_ui_test} tests/${task6_ui_test}.cpp)
        target_link_libraries(${task6_ui_test} PRIVATE pastit_core)
        add_test(NAME ${task6_ui_test} COMMAND ${task6_ui_test})
    endforeach()

    add_executable(openai_client_test tests/openai_client_test.cpp)
    target_link_libraries(openai_client_test PRIVATE pastit_core)
    add_test(NAME openai_client_test COMMAND openai_client_test)

    add_executable(mermaid_prompt_test tests/mermaid_prompt_test.cpp)
    target_link_libraries(mermaid_prompt_test PRIVATE pastit_core)
    add_test(NAME mermaid_prompt_test COMMAND mermaid_prompt_test)

    add_executable(ai_result_state_test tests/ai_result_state_test.cpp)
    target_link_libraries(ai_result_state_test PRIVATE pastit_core)
    add_test(NAME ai_result_state_test COMMAND ai_result_state_test)

    add_executable(path_history_store_test tests/path_history_store_test.cpp)
    target_link_libraries(path_history_store_test PRIVATE pastit_core)
    add_test(NAME path_history_store_test COMMAND path_history_store_test)

    add_executable(clipboard_prune_test tests/clipboard_prune_test.cpp)
    target_link_libraries(clipboard_prune_test PRIVATE pastit_core)
    add_test(NAME clipboard_prune_test COMMAND clipboard_prune_test)

    add_executable(model_catalog_test tests/model_catalog_test.cpp)
    target_link_libraries(model_catalog_test PRIVATE pastit_core)
    add_test(NAME model_catalog_test COMMAND model_catalog_test)

    add_executable(provider_test_request_test tests/provider_test_request_test.cpp)
    target_link_libraries(provider_test_request_test PRIVATE pastit_core)
    add_test(NAME provider_test_request_test COMMAND provider_test_request_test)

    add_executable(ui_text_helpers_test tests/ui_text_helpers_test.cpp)
    target_link_libraries(ui_text_helpers_test PRIVATE pastit_core)
    add_test(NAME ui_text_helpers_test COMMAND ui_text_helpers_test)

    add_executable(recent_paths_panel_id_test tests/recent_paths_panel_id_test.cpp)
    target_link_libraries(recent_paths_panel_id_test PRIVATE pastit_core)
    add_test(NAME recent_paths_panel_id_test COMMAND recent_paths_panel_id_test)

    add_executable(recent_paths_type_icon_test tests/recent_paths_type_icon_test.cpp)
    target_link_libraries(recent_paths_type_icon_test PRIVATE pastit_core)
    add_test(NAME recent_paths_type_icon_test COMMAND recent_paths_type_icon_test)

    if (NOT WIN32)
        add_executable(linux_recent_paths_test tests/linux_recent_paths_test.cpp)
        target_link_libraries(linux_recent_paths_test PRIVATE pastit_core)
        add_test(NAME linux_recent_paths_test COMMAND linux_recent_paths_test)
    endif()

    add_executable(execution_feedback_test tests/execution_feedback_test.cpp)
    target_link_libraries(execution_feedback_test PRIVATE pastit_core)
    add_test(NAME execution_feedback_test COMMAND execution_feedback_test)

    foreach(model_test localization_test recent_paths_model_test settings_model_test)
        add_executable(${model_test} tests/${model_test}.cpp)
        target_link_libraries(${model_test} PRIVATE pastit_core)
        add_test(NAME ${model_test} COMMAND ${model_test})
    endforeach()
    if (NOT WIN32)
        add_executable(single_instance_test tests/single_instance_test.cpp)
        target_link_libraries(single_instance_test PRIVATE pastit_core)
        add_test(NAME single_instance_test COMMAND single_instance_test)
    endif()

    add_executable(ui_state_test tests/ui_state_test.cpp)
    target_link_libraries(ui_state_test PRIVATE pastit_core)
    add_test(NAME ui_state_test COMMAND ui_state_test)

    add_executable(content_detection_test tests/content_detection_test.cpp)
    target_link_libraries(content_detection_test PRIVATE pastit_core)
    add_test(NAME content_detection_test COMMAND content_detection_test)

    add_executable(fast_content_detector_test tests/fast_content_detector_test.cpp)
    target_link_libraries(fast_content_detector_test PRIVATE pastit_core)
    add_test(NAME fast_content_detector_test COMMAND fast_content_detector_test)
    add_executable(graph_data_test tests/graph_data_test.cpp)
    target_link_libraries(graph_data_test PRIVATE pastit_core)
    add_test(NAME graph_data_test COMMAND graph_data_test)

    add_executable(action_catalog_test tests/action_catalog_test.cpp)
    target_link_libraries(action_catalog_test PRIVATE pastit_core)
    add_test(NAME action_catalog_test COMMAND action_catalog_test)

    add_executable(action_catalog_fast_actions_test tests/action_catalog_fast_actions_test.cpp)
    target_link_libraries(action_catalog_fast_actions_test PRIVATE pastit_core)
    add_test(NAME action_catalog_fast_actions_test COMMAND action_catalog_fast_actions_test)

    add_executable(fast_actions_e2e_test tests/fast_actions_e2e_test.cpp)
    target_link_libraries(fast_actions_e2e_test PRIVATE pastit_core)
    add_test(NAME fast_actions_e2e_test COMMAND fast_actions_e2e_test)

    add_executable(djev_response_test tests/djev_response_test.cpp)
    target_link_libraries(djev_response_test PRIVATE pastit_core)
    add_test(NAME djev_response_test COMMAND djev_response_test)

    add_executable(decision_session_test tests/decision_session_test.cpp)
    target_link_libraries(decision_session_test PRIVATE pastit_core)
    add_test(NAME decision_session_test COMMAND decision_session_test)

    add_executable(decision_future_slot_test tests/decision_future_slot_test.cpp)
    target_link_libraries(decision_future_slot_test PRIVATE pastit_core)
    add_test(NAME decision_future_slot_test COMMAND decision_future_slot_test)

    add_executable(executor_test tests/executor_test.cpp)
    target_link_libraries(executor_test PRIVATE pastit_core)
    add_test(NAME executor_test COMMAND executor_test)

    foreach(fast_action_task4_test download_job_test fast_action_executor_test result_state_test)
        add_executable(${fast_action_task4_test} tests/${fast_action_task4_test}.cpp)
        target_link_libraries(${fast_action_task4_test} PRIVATE pastit_core)
        add_test(NAME ${fast_action_task4_test} COMMAND ${fast_action_task4_test})
    endforeach()

    foreach(fast_action_task5_test renderer_service_test image_annotation_test)
        add_executable(${fast_action_task5_test} tests/${fast_action_task5_test}.cpp)
        target_link_libraries(${fast_action_task5_test} PRIVATE pastit_core)
        add_test(NAME ${fast_action_task5_test} COMMAND ${fast_action_task5_test})
    endforeach()

    add_executable(ui_catalog_smoke_test tests/ui_catalog_smoke_test.cpp)
    target_link_libraries(ui_catalog_smoke_test PRIVATE pastit_core)
    add_test(NAME ui_catalog_smoke_test COMMAND ui_catalog_smoke_test)

    add_executable(fast_action_panel_model_test tests/fast_action_panel_model_test.cpp)
    target_link_libraries(fast_action_panel_model_test PRIVATE pastit_core)
    add_test(NAME fast_action_panel_model_test COMMAND fast_action_panel_model_test)

    add_executable(multi_viewport_test tests/multi_viewport_test.cpp)
    target_link_libraries(multi_viewport_test PRIVATE pastit_core)
    add_test(NAME multi_viewport_test COMMAND multi_viewport_test)

    add_executable(desktop_flow_test tests/desktop_flow_test.cpp)
    target_link_libraries(desktop_flow_test PRIVATE pastit_core)
    add_test(NAME desktop_flow_test COMMAND desktop_flow_test)

    add_executable(protocol_fixture_test tests/protocol_fixture_test.cpp)
    target_link_libraries(protocol_fixture_test PRIVATE pastit_core)
    add_test(NAME protocol_fixture_test COMMAND protocol_fixture_test)

    if (NOT WIN32)
        add_executable(test_x11_helpers tests/test_x11_helpers.cpp)
        target_link_libraries(test_x11_helpers PRIVATE pastit_core)
        add_test(NAME test_x11_helpers COMMAND test_x11_helpers)

        add_executable(test_x11_integration tests/test_x11_integration.cpp)
        target_link_libraries(test_x11_integration PRIVATE pastit_core)
        add_test(NAME test_x11_integration COMMAND test_x11_integration)
        set_tests_properties(test_x11_integration PROPERTIES SKIP_RETURN_CODE 77)

        add_executable(test_x11_hotkey tests/test_x11_hotkey.cpp)
        target_link_libraries(test_x11_hotkey PRIVATE pastit_core)
        add_test(NAME test_x11_hotkey COMMAND test_x11_hotkey)
        set_tests_properties(test_x11_hotkey PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    add_executable(djev_payload_budget_test tests/djev_payload_budget_test.cpp)
    target_link_libraries(djev_payload_budget_test PRIVATE pastit_core)
    add_test(NAME djev_payload_budget_test COMMAND djev_payload_budget_test)
    add_executable(clipboard_history_store_test tests/clipboard_history_store_test.cpp)
    target_link_libraries(clipboard_history_store_test PRIVATE pastit_core)
    add_test(NAME clipboard_history_store_test COMMAND clipboard_history_store_test)
    add_executable(clipboard_orphan_cleanup_test tests/clipboard_orphan_cleanup_test.cpp)
    target_link_libraries(clipboard_orphan_cleanup_test PRIVATE pastit_core)
    add_test(NAME clipboard_orphan_cleanup_test COMMAND clipboard_orphan_cleanup_test)
    add_executable(generated_filename_test tests/generated_filename_test.cpp)
    target_link_libraries(generated_filename_test PRIVATE pastit_core)
    add_test(NAME generated_filename_test COMMAND generated_filename_test)
    add_executable(file_operation_confirmation_test tests/file_operation_confirmation_test.cpp)
    target_link_libraries(file_operation_confirmation_test PRIVATE pastit_core)
    add_test(NAME file_operation_confirmation_test COMMAND file_operation_confirmation_test)
    add_executable(path_target_resolver_test tests/path_target_resolver_test.cpp)
    target_link_libraries(path_target_resolver_test PRIVATE pastit_core)
    add_test(NAME path_target_resolver_test COMMAND path_target_resolver_test)
    add_executable(clipboard_history_model_test tests/clipboard_history_model_test.cpp)
    target_link_libraries(clipboard_history_model_test PRIVATE pastit_core)
    add_test(NAME clipboard_history_model_test COMMAND clipboard_history_model_test)

    add_executable(utility_actions_test tests/utility_actions_test.cpp)
    target_link_libraries(utility_actions_test PRIVATE pastit_core)
    add_test(NAME utility_actions_test COMMAND utility_actions_test)

    add_executable(content_profile_test tests/content_profile_test.cpp)
    target_link_libraries(content_profile_test PRIVATE pastit_core)
    add_test(NAME content_profile_test COMMAND content_profile_test)

    add_executable(data_views_test tests/data_views_test.cpp)
    target_link_libraries(data_views_test PRIVATE pastit_core)
    add_test(NAME data_views_test COMMAND data_views_test)

    add_executable(pipeline_test tests/pipeline_test.cpp)
    target_link_libraries(pipeline_test PRIVATE pastit_core)
    add_test(NAME pipeline_test COMMAND pipeline_test)

    add_executable(privacy_test tests/privacy_test.cpp)
    target_link_libraries(privacy_test PRIVATE pastit_core)
    add_test(NAME privacy_test COMMAND privacy_test)

    add_executable(usage_model_test tests/usage_model_test.cpp)
    target_link_libraries(usage_model_test PRIVATE pastit_core)
    add_test(NAME usage_model_test COMMAND usage_model_test)

    add_executable(djev_live_integration tests/djev_live_integration.cpp)
    target_link_libraries(djev_live_integration PRIVATE pastit_core)

    add_executable(openai_live_integration tests/openai_live_integration.cpp)
    target_link_libraries(openai_live_integration PRIVATE pastit_core)

    if (NOT WIN32)
        add_executable(x11_clipboard_seed tests/x11_clipboard_seed.cpp)
        target_link_libraries(x11_clipboard_seed PRIVATE pastit_core)
    endif()
endif()

