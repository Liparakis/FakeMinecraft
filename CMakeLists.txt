cmake_minimum_required(VERSION 3.20)
project(CPP_GAME CXX RC)

if (NOT MINGW)
    message(FATAL_ERROR "Fake Minecraft builds with MinGW GCC only (for example CLion's bundled MinGW toolchain).")
endif ()

include(${CMAKE_SOURCE_DIR}/CMakeScripts/PowerShell.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# Vulkan headers, vulkan-1 import library and glslc all come from the Vulkan SDK (VULKAN_SDK).
find_package(Vulkan REQUIRED COMPONENTS glslc)

set(DEP_BIN ${CMAKE_SOURCE_DIR}/dependencies/bin)
set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)
# The game runs from <dir>/game and loads assets from <dir>/assets.
set(GAME_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/build CACHE PATH "Where the playable game folder is assembled")
set(BUILD_ASSETS_DIR ${GAME_OUTPUT_DIR}/assets)

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS src/*.h src/*.cpp)

add_executable(CPP_GAME ${SOURCE_FILES} src/app.rc)

target_include_directories(CPP_GAME PRIVATE
        ${CMAKE_SOURCE_DIR}/dependencies/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/Core/headers
        ${CMAKE_SOURCE_DIR}/src/World/headers
        ${CMAKE_SOURCE_DIR}/src/World/Chunk/headers
        ${CMAKE_SOURCE_DIR}/src/Renderer
        ${CMAKE_SOURCE_DIR}/src/Renderer/headers
        ${CMAKE_SOURCE_DIR}/src/Renderer/Vertices
        ${CMAKE_SOURCE_DIR}/src/UI/headers
)

target_compile_definitions(CPP_GAME PRIVATE
        SDL_MAIN_HANDLED
)

# SDL2 and SDL2_mixer are C libraries, so MinGW links their DLLs directly.
target_link_libraries(CPP_GAME PRIVATE
        Vulkan::Vulkan
        ${DEP_BIN}/SDL2.dll
        ${DEP_BIN}/SDL2_mixer.dll
        -static-libgcc -static-libstdc++
)

# std::thread needs winpthread at runtime; ship the compiler's copy next to the exe.
get_filename_component(MINGW_BIN ${CMAKE_CXX_COMPILER} DIRECTORY)
set(WINPTHREAD_DLL ${MINGW_BIN}/libwinpthread-1.dll)

set_target_properties(CPP_GAME PROPERTIES
        WIN32_EXECUTABLE TRUE
        RUNTIME_OUTPUT_DIRECTORY "${GAME_OUTPUT_DIR}/game")

# Shaders are compiled to SPIR-V next to the other runtime assets.
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS ${ASSETS_DIR}/shaders/*.vert ${ASSETS_DIR}/shaders/*.frag)
set(SPIRV_FILES)
foreach (SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV ${BUILD_ASSETS_DIR}/shaders/${SHADER_NAME}.spv)
    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${BUILD_ASSETS_DIR}/shaders
            COMMAND Vulkan::glslc ${SHADER} -o ${SPIRV}
            DEPENDS ${SHADER} ${ASSETS_DIR}/shaders/common.glsl
            COMMENT "Compiling ${SHADER_NAME}")
    list(APPEND SPIRV_FILES ${SPIRV})
endforeach ()
add_custom_target(Shaders ALL DEPENDS ${SPIRV_FILES})
add_dependencies(CPP_GAME Shaders)

add_custom_command(TARGET CPP_GAME POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${DEP_BIN} $<TARGET_FILE_DIR:CPP_GAME>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WINPTHREAD_DLL} $<TARGET_FILE_DIR:CPP_GAME>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${ASSETS_DIR}/fonts ${BUILD_ASSETS_DIR}/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${ASSETS_DIR}/sounds ${BUILD_ASSETS_DIR}/sounds
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${ASSETS_DIR}/sprites ${BUILD_ASSETS_DIR}/sprites
)

# Desktop-style shortcut with the game icon in the build folder.
execute_powershell(
        "$ErrorActionPreference = 'Stop';
        $s=(New-Object -COMObject WScript.Shell).CreateShortcut('${GAME_OUTPUT_DIR}/CPP_GAME.lnk');
        $s.TargetPath='${GAME_OUTPUT_DIR}/game/CPP_GAME.exe';
        $s.WorkingDirectory='${GAME_OUTPUT_DIR}/game';
        $s.IconLocation='${CMAKE_SOURCE_DIR}/assets/sprites/logo.ico';
        $s.Save()"
        OUTPUT_VARIABLE PS_OUTPUT
        RESULT_VARIABLE PS_RESULT
)
