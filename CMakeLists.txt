cmake_minimum_required(VERSION 3.20)
# The version: package.ps1, the installer, README and the DLL all take it from here.
project(AlphaViewsXR VERSION 1.4.3 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)

# Headers only: the layer must not link the OpenXR loader.
include(FetchContent)
FetchContent_Declare(openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.65
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(openxr)

# Precompile shaders with fxc into headers (no d3dcompiler DLL needed at runtime).
file(GLOB FXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64/fxc.exe")
if(NOT FXC_CANDIDATES)
    message(FATAL_ERROR "fxc.exe not found: install the Windows 10/11 SDK")
endif()
list(SORT FXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
list(GET FXC_CANDIDATES 0 FXC)
message(STATUS "Using fxc: ${FXC}")
set(SHADER_SRC ${CMAKE_SOURCE_DIR}/shaders.hlsl)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})
set(SHADER_HEADERS)
foreach(entry vsFull:vs_4_0 vsMesh:vs_4_0 vsSphere:vs_4_0 psConst:ps_4_0 psColor:ps_4_0 psBox:ps_4_0 vsPanel:vs_4_0 psPanel:ps_4_0 vsSphereD:vs_4_0 psHandD:ps_4_0 vsIcon:vs_4_0 psIcon:ps_4_0)
    string(REPLACE ":" ";" parts ${entry})
    list(GET parts 0 name)
    list(GET parts 1 profile)
    add_custom_command(OUTPUT ${SHADER_OUT}/${name}.h
        COMMAND ${FXC} /nologo /O3 /T ${profile} /E ${name} /Vn g_${name}_bc /Fh ${SHADER_OUT}/${name}.h ${SHADER_SRC}
        DEPENDS ${SHADER_SRC} VERBATIM)
    list(APPEND SHADER_HEADERS ${SHADER_OUT}/${name}.h)
endforeach()

set(LAYER_NAME XR_APILAYER_GMO_alpha_views_xr)
add_library(${LAYER_NAME} SHARED layer.cpp version.rc guide_icons.rc ${SHADER_HEADERS})
target_include_directories(${LAYER_NAME} PRIVATE ${openxr_SOURCE_DIR}/include ${SHADER_OUT})
target_compile_options(${LAYER_NAME} PRIVATE /utf-8)
target_compile_definitions(${LAYER_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
    LAYER_VERSION="${PROJECT_VERSION}"
    VER_MAJOR=${PROJECT_VERSION_MAJOR} VER_MINOR=${PROJECT_VERSION_MINOR} VER_PATCH=${PROJECT_VERSION_PATCH})
target_link_libraries(${LAYER_NAME} PRIVATE d3d11 gdi32)
# Static C runtime: no Visual C++ redistributable needed on users' PCs.
set_target_properties(${LAYER_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_custom_command(TARGET ${LAYER_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/${LAYER_NAME}.json $<TARGET_FILE_DIR:${LAYER_NAME}>)
