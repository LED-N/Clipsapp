cmake_minimum_required(VERSION 3.22)
project(Clipsapp VERSION 0.8.2 LANGUAGES CXX)

# The GitHub repository whose releases the published application follows for its updates,
# written "owner/name". Left empty, the application never looks for updates.
set(CLIPSAPP_UPDATE_REPOSITORY "LED-N/Clipsapp")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network)
qt_standard_project_setup()

add_library(clipsapp_core STATIC
    src/core/config.cpp src/core/config.h
    src/core/config_store.cpp src/core/config_store.h
    src/core/clip_library.cpp src/core/clip_library.h
    src/core/replay_buffer.cpp src/core/replay_buffer.h
    src/core/audio_routing.cpp src/core/audio_routing.h
    src/core/game_identity.cpp src/core/game_identity.h
    src/core/update.cpp src/core/update.h
    src/core/track_mix.cpp src/core/track_mix.h
    src/platform/game_detection.cpp src/platform/game_detection.h
    src/platform/start_menu.cpp src/platform/start_menu.h
    src/media/clip_edit.cpp src/media/clip_edit.h
)
target_include_directories(clipsapp_core PUBLIC src)
target_link_libraries(clipsapp_core PUBLIC Qt6::Core)
if(WIN32)
    target_compile_definitions(clipsapp_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(clipsapp_core PRIVATE user32 version)
endif()

if(WIN32)
    set(CLIPSAPP_PLATFORM_SOURCE src/platform/windows_catalog.cpp src/platform/windows_audio.cpp src/platform/windows_audio.h src/platform/windows_video.cpp src/platform/windows_video.h src/capture/windows_capture.cpp src/capture/audio_stream.cpp src/capture/audio_stream.h)
    set(CLIPSAPP_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/.tools/ffmpeg/ffmpeg-n9.0.2-24-gfd5d616c29-win64-lgpl-shared-9.0" CACHE PATH "FFmpeg shared SDK")
    foreach(component avcodec avformat avutil swresample swscale)
        find_library(CLIPSAPP_${component} NAMES ${component} PATHS "${CLIPSAPP_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    endforeach()
    add_library(clipsapp_media INTERFACE)
    target_include_directories(clipsapp_media INTERFACE "${CLIPSAPP_FFMPEG_ROOT}/include")
    target_link_libraries(clipsapp_media INTERFACE ${CLIPSAPP_avcodec} ${CLIPSAPP_avformat} ${CLIPSAPP_avutil} ${CLIPSAPP_swresample} ${CLIPSAPP_swscale} d3d11 runtimeobject)
    set(CLIPSAPP_AUDIO_OUTPUT src/platform/windows_audio_output.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CLIPSAPP_PLATFORM_SOURCE src/platform/linux_catalog.cpp)
    set(CLIPSAPP_AUDIO_OUTPUT src/platform/linux_audio_output.cpp)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswresample libswscale)
    add_library(clipsapp_media INTERFACE)
    target_link_libraries(clipsapp_media INTERFACE PkgConfig::FFMPEG)
else()
    message(FATAL_ERROR "Clipsapp targets Windows and Linux.")
endif()

add_library(clipsapp_editor STATIC
    src/media/clip_reader.cpp src/media/clip_reader.h
    src/media/clip_player.cpp src/media/clip_player.h
    src/media/clip_exporter.cpp src/media/clip_exporter.h
    src/media/clip_analysis.cpp src/media/clip_analysis.h
    src/media/gpu_video.cpp src/media/gpu_video.h
    src/media/audio_output.h ${CLIPSAPP_AUDIO_OUTPUT}
    src/platform/fine_wait.h
)
target_link_libraries(clipsapp_editor PUBLIC clipsapp_core Qt6::Gui PRIVATE clipsapp_media)
if(WIN32)
    target_compile_definitions(clipsapp_editor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(clipsapp_editor PRIVATE ole32 uuid mmdevapi winmm d3d11 dxgi dxguid)
endif()

set(CLIPSAPP_APP_SOURCES
    src/ui/main_window.cpp src/ui/main_window.h
    src/ui/theme.cpp src/ui/theme.h
    src/ui/widgets.cpp src/ui/widgets.h
    src/ui/replay_strip.cpp src/ui/replay_strip.h
    src/ui/thumbnail_cache.cpp src/ui/thumbnail_cache.h
    src/ui/clip_list.cpp src/ui/clip_list.h
    src/ui/library_page.cpp src/ui/library_page.h
    src/ui/sharing_page.cpp src/ui/sharing_page.h
    src/ui/settings_page.cpp src/ui/settings_page.h
    src/ui/source_dialog.cpp src/ui/source_dialog.h
    src/ui/onboarding_page.cpp src/ui/onboarding_page.h
    src/ui/discord_batch.cpp src/ui/discord_batch.h
    src/ui/discord_dialog.cpp src/ui/discord_dialog.h
    src/ui/clip_editor.cpp src/ui/clip_editor.h
    src/ui/clip_viewer.cpp src/ui/clip_viewer.h
    src/ui/layout_keys.cpp src/ui/layout_keys.h
    src/ui/update_service.cpp src/ui/update_service.h
    src/ui/video_canvas.cpp src/ui/video_canvas.h
    src/ui/video_surface.cpp src/ui/video_surface.h
    src/ui/clip_timeline.cpp src/ui/clip_timeline.h
    src/ui/clip_feedback.cpp src/ui/clip_feedback.h
    src/platform/source_catalog.h ${CLIPSAPP_PLATFORM_SOURCE}
    src/platform/global_hotkey.cpp src/platform/global_hotkey.h
    src/capture/replay_engine.cpp src/capture/replay_engine.h
)
qt_add_executable(clipsapp src/main.cpp ${CLIPSAPP_APP_SOURCES})
target_link_libraries(clipsapp PRIVATE clipsapp_core clipsapp_editor Qt6::Widgets Qt6::Concurrent Qt6::Network)
if(WIN32)
    target_link_libraries(clipsapp PRIVATE winmm)
    # The icon of the executable: what Explorer, the Start menu and the taskbar show for it.
    enable_language(RC)
    # Compiled on its own, without the include folders of the application: windres hands them to
    # its preprocessor unquoted, and starts that preprocessor by its full path. Both fail as soon
    # as a path holds a space, which the folder of the project may.
    add_library(clipsapp_icon OBJECT resources/clipsapp.rc)
    set_target_properties(clipsapp_icon PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    if(MINGW)
        set_source_files_properties(resources/clipsapp.rc PROPERTIES COMPILE_OPTIONS "--preprocessor=gcc;--preprocessor-arg=-E;--preprocessor-arg=-xc;--preprocessor-arg=-DRC_INVOKED")
    endif()
    target_sources(clipsapp PRIVATE $<TARGET_OBJECTS:clipsapp_icon>)
endif()
target_compile_definitions(clipsapp PRIVATE CLIPSAPP_VERSION="${PROJECT_VERSION}" CLIPSAPP_UPDATE_REPOSITORY="${CLIPSAPP_UPDATE_REPOSITORY}")
if(WIN32)
    target_compile_definitions(clipsapp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(clipsapp PRIVATE clipsapp_media ole32 uuid user32 mmdevapi)
    file(GLOB CLIPSAPP_MEDIA_DLLS "${CLIPSAPP_FFMPEG_ROOT}/bin/*.dll")
    list(APPEND CLIPSAPP_MEDIA_DLLS "${CLIPSAPP_FFMPEG_ROOT}/bin/ffmpeg.exe")
    add_custom_command(TARGET clipsapp POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CLIPSAPP_MEDIA_DLLS} $<TARGET_FILE_DIR:clipsapp> COMMAND_EXPAND_LISTS)
    set_target_properties(clipsapp PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(MSVC)
    target_compile_options(clipsapp_core PRIVATE /W4 /utf-8)
    target_compile_options(clipsapp PRIVATE /W4 /utf-8)
else()
    target_compile_options(clipsapp_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(clipsapp PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(clipsapp_core_tests tests/core_tests.cpp)
    target_link_libraries(clipsapp_core_tests PRIVATE clipsapp_core Qt6::Test)
    add_test(NAME clipsapp_core_tests COMMAND clipsapp_core_tests)
    qt_add_executable(clipsapp_update_probe tests/update_probe.cpp src/ui/update_service.cpp src/ui/update_service.h)
    target_link_libraries(clipsapp_update_probe PRIVATE clipsapp_core Qt6::Network)
    set_target_properties(clipsapp_update_probe PROPERTIES WIN32_EXECUTABLE FALSE)
    qt_add_executable(clipsapp_trash_probe tests/trash_probe.cpp)
    target_link_libraries(clipsapp_trash_probe PRIVATE clipsapp_core)
    set_target_properties(clipsapp_trash_probe PROPERTIES WIN32_EXECUTABLE FALSE)
    if(WIN32)
        qt_add_executable(clipsapp_media_tests tests/media_tests.cpp src/capture/replay_engine.cpp src/capture/replay_engine.h src/capture/windows_capture.cpp src/platform/windows_audio.cpp src/platform/windows_video.cpp src/capture/audio_stream.cpp)
        target_compile_definitions(clipsapp_media_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(clipsapp_media_tests PRIVATE clipsapp_core clipsapp_media Qt6::Test Qt6::Concurrent ole32 uuid user32 mmdevapi)
        add_test(NAME clipsapp_media_tests COMMAND clipsapp_media_tests)
        set_tests_properties(clipsapp_media_tests PROPERTIES TIMEOUT 35)
        qt_add_executable(clipsapp_editor_tests tests/editor_tests.cpp ${CLIPSAPP_APP_SOURCES})
        target_compile_definitions(clipsapp_editor_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN CLIPSAPP_VERSION="${PROJECT_VERSION}")
        target_link_libraries(clipsapp_editor_tests PRIVATE clipsapp_editor clipsapp_media Qt6::Widgets Qt6::Test Qt6::Concurrent Qt6::Network ole32 uuid user32 mmdevapi winmm)
        set_target_properties(clipsapp_editor_tests PROPERTIES WIN32_EXECUTABLE FALSE)
        add_test(NAME clipsapp_editor_tests COMMAND clipsapp_editor_tests -o "${CMAKE_CURRENT_BINARY_DIR}/editor-test-results.txt,txt")
        set_tests_properties(clipsapp_editor_tests PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=windows")
        qt_add_executable(clipsapp_player_probe tests/player_probe.cpp)
        target_link_libraries(clipsapp_player_probe PRIVATE clipsapp_editor Qt6::Gui)
        qt_add_executable(clipsapp_presentation_probe tests/presentation_probe.cpp)
        target_compile_definitions(clipsapp_presentation_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(clipsapp_presentation_probe PRIVATE clipsapp_editor Qt6::Gui)
        set_target_properties(clipsapp_presentation_probe PROPERTIES WIN32_EXECUTABLE FALSE)
        qt_add_executable(clipsapp_compatibility_probe tests/compatibility_probe.cpp)
        target_link_libraries(clipsapp_compatibility_probe PRIVATE clipsapp_editor Qt6::Gui)
        qt_add_executable(clipsapp_share_probe tests/share_probe.cpp)
        target_link_libraries(clipsapp_share_probe PRIVATE clipsapp_editor Qt6::Gui)
        set_target_properties(clipsapp_share_probe PROPERTIES WIN32_EXECUTABLE FALSE)
        qt_add_executable(clipsapp_memory_probe tests/memory_probe.cpp ${CLIPSAPP_APP_SOURCES})
        target_compile_definitions(clipsapp_memory_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN CLIPSAPP_VERSION="${PROJECT_VERSION}")
        target_link_libraries(clipsapp_memory_probe PRIVATE clipsapp_editor clipsapp_media Qt6::Widgets Qt6::Concurrent Qt6::Network ole32 uuid user32 mmdevapi winmm)
        qt_add_executable(clipsapp_preview_probe tests/preview_probe.cpp ${CLIPSAPP_APP_SOURCES})
        target_compile_definitions(clipsapp_preview_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN CLIPSAPP_VERSION="${PROJECT_VERSION}")
        target_link_libraries(clipsapp_preview_probe PRIVATE clipsapp_editor clipsapp_media Qt6::Widgets Qt6::Concurrent Qt6::Network ole32 uuid user32 mmdevapi winmm)
        set_target_properties(clipsapp_preview_probe PROPERTIES WIN32_EXECUTABLE FALSE)
        qt_add_executable(clipsapp_application_mix_probe tests/application_mix_probe.cpp src/platform/windows_audio.cpp src/capture/audio_stream.cpp)
        target_compile_definitions(clipsapp_application_mix_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(clipsapp_application_mix_probe PRIVATE clipsapp_editor clipsapp_media ole32 uuid mmdevapi)
        qt_add_executable(clipsapp_capture_probe tests/capture_probe.cpp src/capture/windows_capture.cpp src/platform/windows_audio.cpp src/platform/windows_video.cpp src/platform/windows_catalog.cpp src/capture/audio_stream.cpp)
        target_compile_definitions(clipsapp_capture_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(clipsapp_capture_probe PRIVATE clipsapp_core clipsapp_media Qt6::Gui Qt6::Concurrent ole32 uuid user32 mmdevapi)
        set_target_properties(clipsapp_player_probe clipsapp_compatibility_probe clipsapp_memory_probe clipsapp_capture_probe PROPERTIES WIN32_EXECUTABLE FALSE)
        qt_add_executable(clipsapp_audio_tests tests/audio_tests.cpp src/capture/audio_stream.cpp)
        target_link_libraries(clipsapp_audio_tests PRIVATE clipsapp_core clipsapp_media Qt6::Test)
        add_test(NAME clipsapp_audio_tests COMMAND clipsapp_audio_tests)
        qt_add_executable(clipsapp_hotkey_tests tests/hotkey_tests.cpp src/platform/global_hotkey.cpp src/platform/global_hotkey.h)
        target_include_directories(clipsapp_hotkey_tests PRIVATE src)
        target_compile_definitions(clipsapp_hotkey_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(clipsapp_hotkey_tests PRIVATE Qt6::Test Qt6::Gui user32)
        add_test(NAME clipsapp_hotkey_tests COMMAND clipsapp_hotkey_tests)
        set_tests_properties(clipsapp_hotkey_tests PROPERTIES TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=windows")
    endif()
endif()
install(TARGETS clipsapp RUNTIME DESTINATION bin)

