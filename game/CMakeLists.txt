# SPDX-License-Identifier: GPL-2.0-or-later
# Built as part of the Azahar source tree (see setup_game.bat).
#   TFH_IMGUI_DIR       Dear ImGui checkout (required)
#   TFH_STEAMWORKS_DIR  Steamworks SDK "sdk" folder (optional; enables online)

if (NOT TFH_IMGUI_DIR OR NOT EXISTS "${TFH_IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "TFH_IMGUI_DIR must point to a Dear ImGui checkout")
endif()

add_library(tfh_imgui STATIC
    ${TFH_IMGUI_DIR}/imgui.cpp
    ${TFH_IMGUI_DIR}/imgui_draw.cpp
    ${TFH_IMGUI_DIR}/imgui_tables.cpp
    ${TFH_IMGUI_DIR}/imgui_widgets.cpp
    ${TFH_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${TFH_IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(tfh_imgui PUBLIC ${TFH_IMGUI_DIR} ${TFH_IMGUI_DIR}/backends)
target_link_libraries(tfh_imgui PUBLIC SDL2::SDL2)

# The project's own 3DS container parser (src/, same code as tools/ctrinspect),
# used by the setup assistant to validate the game file.
get_filename_component(TFH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
add_library(tfh_formats STATIC
    ${TFH_ROOT}/src/common/sha256.cpp
    ${TFH_ROOT}/src/formats/blz.cpp
    ${TFH_ROOT}/src/formats/ctr.cpp
    ${TFH_ROOT}/src/formats/romfs.cpp
    ${TFH_ROOT}/src/inspect/inspector.cpp
    ${TFH_ROOT}/src/inspect/report.cpp
)
target_include_directories(tfh_formats PUBLIC ${TFH_ROOT}/src)
target_compile_features(tfh_formats PUBLIC cxx_std_20)

add_executable(tfh_game
    main.cpp
    game_file.cpp
    game_file.h
    any_input.cpp
    any_input.h
    config.cpp
    config.h
    crash_handler.cpp
    crash_handler.h
    discord_rpc.cpp
    discord_rpc.h
    levels.h
    romfs_watch.cpp
    romfs_watch.h
    sdl_window.cpp
    sdl_window.h
    steam_net.cpp
    steam_net.h
    ui.cpp
    ui.h
)
set_target_properties(tfh_game PROPERTIES OUTPUT_NAME "TriforceRecompiled")

# Discord application used for Rich Presence (name "Triforce Recompiled", art assets).
set(TFH_DISCORD_APP_ID "1571592813689315509" CACHE STRING "Discord application ID for Rich Presence")
target_compile_definitions(tfh_game PRIVATE TFH_DISCORD_APP_ID="${TFH_DISCORD_APP_ID}")

target_link_libraries(tfh_game PRIVATE
    audio_core citra_common citra_core input_common network video_core tfh_imgui tfh_formats
    SDL2::SDL2 Boost::boost ${PLATFORM_LIBRARIES} Threads::Threads)

if (ENABLE_VULKAN)
    target_link_libraries(tfh_game PRIVATE vulkan-headers)
endif()

if (TFH_STEAMWORKS_DIR AND EXISTS "${TFH_STEAMWORKS_DIR}/public/steam/steam_api.h")
    message(STATUS "Tri Force Heroes PC: Steamworks enabled (${TFH_STEAMWORKS_DIR})")
    target_compile_definitions(tfh_game PRIVATE TFH_HAS_STEAM)
    target_include_directories(tfh_game PRIVATE "${TFH_STEAMWORKS_DIR}/public")
    if (WIN32)
        set(TFH_STEAM_LIB "${TFH_STEAMWORKS_DIR}/redistributable_bin/win64/steam_api64.lib")
        set(TFH_STEAM_DLL "${TFH_STEAMWORKS_DIR}/redistributable_bin/win64/steam_api64.dll")
    else()
        set(TFH_STEAM_LIB "${TFH_STEAMWORKS_DIR}/redistributable_bin/linux64/libsteam_api.so")
        set(TFH_STEAM_DLL "${TFH_STEAM_LIB}")
    endif()
    target_link_libraries(tfh_game PRIVATE "${TFH_STEAM_LIB}")
    add_custom_command(TARGET tfh_game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TFH_STEAM_DLL}" "$<TARGET_FILE_DIR:tfh_game>")
else()
    message(STATUS "Tri Force Heroes PC: Steamworks SDK not found - building without online support")
endif()

if (MSVC)
    target_compile_options(tfh_game PRIVATE /utf-8)
endif()
if (WIN32)
    target_link_libraries(tfh_game PRIVATE dbghelp comdlg32)
endif()
