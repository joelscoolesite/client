cmake_minimum_required(VERSION 3.20)
project(BedrockWaypoints LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Minecraft Bedrock is 64-bit: build for x64 (cmake -A x64).")
endif()

include(FetchContent)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
    # Only download; minhook's own CMakeLists is too old for CMake 4.
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(imgui minhook)

add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

add_library(BedrockWaypoints SHARED
    src/dllmain.cpp
    src/config.cpp
    src/game.cpp
    src/render.cpp
    src/ui.cpp
    src/waypoints.cpp)
target_link_libraries(BedrockWaypoints PRIVATE imgui minhook d3d11 d3d12 dxgi d3dcompiler dwmapi)
target_compile_definitions(BedrockWaypoints PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

if(MSVC)
    # Static CRT so the DLL does not need the VC++ redistributable inside the game process.
    set_property(TARGET BedrockWaypoints imgui minhook PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(BedrockWaypoints PRIVATE /W4 /utf-8)
else()
    target_link_options(BedrockWaypoints PRIVATE -static -static-libgcc -static-libstdc++)
endif()
