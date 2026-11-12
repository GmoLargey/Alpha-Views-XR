cmake_minimum_required(VERSION 3.20)
project(AlphaPassthroughTest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT: runs without VC++ redist
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.65
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(openxr)
set_target_properties(openxr_loader PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")  # match static CRT

add_executable(AlphaPassthroughTest main.cpp)
target_compile_definitions(AlphaPassthroughTest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(AlphaPassthroughTest PRIVATE openxr_loader d3d11 dxgi d3dcompiler)

add_executable(ListExtensions listext.cpp)
target_link_libraries(ListExtensions PRIVATE openxr_loader)
