cmake_minimum_required(VERSION 3.20)
# The version: package.ps1, the installer, README and the DLL all take it from here.
project(AlphaViewsXR VERSION 1.5.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32 AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Downloads (once, into the build folder).
set(DEPS ${CMAKE_BINARY_DIR}/deps)
function(fetch url dest)
    if(NOT EXISTS ${dest})
        file(DOWNLOAD ${url} ${dest} STATUS st TLS_VERIFY ON)
        list(GET st 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE ${dest})
            message(FATAL_ERROR "Couldn't download ${url}: ${st}")
        endif()
    endif()
endfunction()

# OpenXR headers only: the layer must not link the OpenXR loader.
include(FetchContent)
FetchContent_Declare(openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.65
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(openxr)

# Vulkan headers (Apache-2.0 / MIT): Vulkan games. The Vulkan loader is the game's own.
set(VULKAN_TAG vulkan-sdk-1.4.357.0)
set(VULKAN_DIR ${DEPS}/Vulkan-Headers-${VULKAN_TAG})
if(NOT EXISTS ${VULKAN_DIR}/include/vulkan/vulkan.h)
    fetch(https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/${VULKAN_TAG}.tar.gz ${DEPS}/vulkan-headers.tar.gz)
    file(ARCHIVE_EXTRACT INPUT ${DEPS}/vulkan-headers.tar.gz DESTINATION ${DEPS})
endif()

set(SHADER_SRC ${CMAKE_SOURCE_DIR}/shaders.hlsl)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})
set(SHADER_HEADERS)
set(SHADER_ENTRIES vsFull:vs vsMesh:vs vsSphere:vs psConst:ps psColor:ps psBox:ps vsPanel:vs psPanel:ps
                   vsSphereD:vs psHandD:ps vsIcon:vs psIcon:ps)

# D3D11 / D3D12 games (Windows): precompile with fxc into headers (no d3dcompiler DLL needed at runtime).
if(WIN32)
    file(GLOB FXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64/fxc.exe")
    if(NOT FXC_CANDIDATES)
        message(FATAL_ERROR "fxc.exe not found: install the Windows 10/11 SDK")
    endif()
    list(SORT FXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    list(GET FXC_CANDIDATES 0 FXC)
    message(STATUS "Using fxc: ${FXC}")
    foreach(entry ${SHADER_ENTRIES})
        string(REPLACE ":" ";" parts ${entry})
        list(GET parts 0 name)
        list(GET parts 1 stage)
        add_custom_command(OUTPUT ${SHADER_OUT}/${name}.h
            COMMAND ${FXC} /nologo /O3 /T ${stage}_4_0 /E ${name} /Vn g_${name}_bc /Fh ${SHADER_OUT}/${name}.h ${SHADER_SRC}
            DEPENDS ${SHADER_SRC} VERBATIM)
        list(APPEND SHADER_HEADERS ${SHADER_OUT}/${name}.h)
    endforeach()
endif()

# Vulkan games: the same shaders as SPIR-V, compiled by dxc (Vulkan SDK, or DXC on the PATH). Without
# dxc the copies in spirv/ are used (made by dxc from the same shaders.hlsl; package.ps1 refreshes them).
# Bindings: cbuffers b0-b2 -> 0-2, textures t0-t1 -> 10-11, sampler s0 -> 20 (set 0).
file(GLOB DXC_CANDIDATES "C:/VulkanSDK/*/Bin/dxc.exe" "$ENV{VULKAN_SDK}/Bin/dxc.exe" "$ENV{VULKAN_SDK}/bin/dxc")
if(DXC_CANDIDATES)
    list(SORT DXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    list(GET DXC_CANDIDATES 0 DXC)
else()
    find_program(DXC NAMES dxc)
endif()
set(SPIRV_DIR ${CMAKE_SOURCE_DIR}/spirv)
if(DXC)
    message(STATUS "Using dxc: ${DXC}")
    set(SPIRV_DIR ${SHADER_OUT})
    foreach(entry ${SHADER_ENTRIES})
        string(REPLACE ":" ";" parts ${entry})
        list(GET parts 0 name)
        list(GET parts 1 stage)
        set(invert)
        if(stage STREQUAL "vs")
            set(invert -fvk-invert-y)
        endif()
        add_custom_command(OUTPUT ${SHADER_OUT}/${name}.spv.h
            COMMAND ${DXC} -nologo -HV 2018 -spirv -fvk-use-dx-layout ${invert} -fspv-target-env=vulkan1.0
                    -fvk-b-shift 0 0 -fvk-t-shift 10 0 -fvk-s-shift 20 0 -O3 -T ${stage}_6_0 -E ${name}
                    -Vn g_${name}_spv -Fh ${SHADER_OUT}/${name}.spv.h ${SHADER_SRC}
            DEPENDS ${SHADER_SRC} VERBATIM)
        list(APPEND SHADER_HEADERS ${SHADER_OUT}/${name}.spv.h)
    endforeach()
else()
    message(STATUS "dxc not found: using the SPIR-V shaders in spirv/")
endif()

set(LAYER_NAME XR_APILAYER_GMO_alpha_views_xr)
set(LAYER_SOURCES layer.cpp render_vk.h guide_font.h ${SHADER_HEADERS})
if(WIN32)
    list(APPEND LAYER_SOURCES version.rc guide_icons.rc)
endif()
add_library(${LAYER_NAME} SHARED ${LAYER_SOURCES})
target_include_directories(${LAYER_NAME} PRIVATE ${openxr_SOURCE_DIR}/include ${VULKAN_DIR}/include ${SHADER_OUT} ${SPIRV_DIR})
target_compile_definitions(${LAYER_NAME} PRIVATE
    LAYER_VERSION="${PROJECT_VERSION}"
    VER_MAJOR=${PROJECT_VERSION_MAJOR} VER_MINOR=${PROJECT_VERSION_MINOR} VER_PATCH=${PROJECT_VERSION_PATCH})

if(WIN32)
    target_compile_options(${LAYER_NAME} PRIVATE /utf-8)
    target_compile_definitions(${LAYER_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(${LAYER_NAME} PRIVATE d3d11 gdi32)
    # Static C runtime: no Visual C++ redistributable needed on users' PCs.
    set_target_properties(${LAYER_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set(LAYER_MANIFEST ${CMAKE_SOURCE_DIR}/${LAYER_NAME}.json)
else()
    # Linux: DirectXMath (MIT, header only; it wants a sal.h: linux/sal.h, empty annotations), and
    # stb_truetype (public domain / MIT) for the guide panel's text.
    set(DXMATH_TAG jun2026)
    set(DXMATH_DIR ${DEPS}/DirectXMath-${DXMATH_TAG})
    if(NOT EXISTS ${DXMATH_DIR}/Inc/DirectXMath.h)
        fetch(https://github.com/microsoft/DirectXMath/archive/refs/tags/${DXMATH_TAG}.tar.gz ${DEPS}/directxmath.tar.gz)
        file(ARCHIVE_EXTRACT INPUT ${DEPS}/directxmath.tar.gz DESTINATION ${DEPS})
    endif()
    set(STB_COMMIT 9600522de53973336fa033a289858a4354cbb04c)
    foreach(f stb_truetype.h LICENSE)
        fetch(https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/${f} ${DEPS}/stb/${f})
    endforeach()
    # The guide panel's gesture pictures, built in (Windows has them as resources).
    set(GESTURE_HDR ${CMAKE_BINARY_DIR}/gestures/gesture_bmps.h)
    file(GLOB GESTURE_BMPS ${CMAKE_SOURCE_DIR}/packaging/gestures/*.bmp)
    add_custom_command(OUTPUT ${GESTURE_HDR}
        COMMAND ${CMAKE_COMMAND} -DOUT=${GESTURE_HDR} -DDIR=${CMAKE_SOURCE_DIR}/packaging/gestures
                -DRC=${CMAKE_SOURCE_DIR}/guide_icons.rc -P ${CMAKE_SOURCE_DIR}/linux/embed_bmps.cmake
        DEPENDS ${GESTURE_BMPS} ${CMAKE_SOURCE_DIR}/guide_icons.rc ${CMAKE_SOURCE_DIR}/linux/embed_bmps.cmake VERBATIM)
    target_sources(${LAYER_NAME} PRIVATE ${GESTURE_HDR})
    target_include_directories(${LAYER_NAME} PRIVATE ${DXMATH_DIR}/Inc ${CMAKE_SOURCE_DIR}/linux ${DEPS}/stb ${CMAKE_BINARY_DIR}/gestures)
    target_compile_options(${LAYER_NAME} PRIVATE -fvisibility=hidden -Wno-unknown-pragmas)
    target_link_libraries(${LAYER_NAME} PRIVATE dl pthread)
    # The C++ runtime is built in, so the layer loads into any game (and the Steam runtime).
    target_link_options(${LAYER_NAME} PRIVATE -static-libstdc++ -static-libgcc -Wl,--no-undefined -Wl,--exclude-libs,ALL
        -Wl,--version-script=${CMAKE_SOURCE_DIR}/linux/exports.map)
    set_target_properties(${LAYER_NAME} PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/linux/exports.map)
    set_target_properties(${LAYER_NAME} PROPERTIES PREFIX "lib")
    set(LAYER_MANIFEST ${CMAKE_SOURCE_DIR}/linux/${LAYER_NAME}.json)
endif()

add_custom_command(TARGET ${LAYER_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAYER_MANIFEST} $<TARGET_FILE_DIR:${LAYER_NAME}>/${LAYER_NAME}.json)

# Test harness (not shipped): a pretend runtime + game that drives the layer and saves what it drew
# (tests/run_harness.ps1). Needs the Vulkan loader to link against (Vulkan SDK, or libvulkan-dev).
option(AVXR_HARNESS "Build the test harness" OFF)
if(AVXR_HARNESS)
    find_package(Vulkan REQUIRED)
    set(STB_COMMIT 9600522de53973336fa033a289858a4354cbb04c)
    fetch(https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/stb_image_write.h ${DEPS}/stb/stb_image_write.h)
    add_executable(xr_harness tests/xr_harness.cpp)
    target_include_directories(xr_harness PRIVATE ${openxr_SOURCE_DIR}/include ${VULKAN_DIR}/include ${DEPS}/stb)
    target_link_libraries(xr_harness PRIVATE Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(xr_harness PRIVATE d3d11)
        target_compile_definitions(xr_harness PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_link_libraries(xr_harness PRIVATE dl)
    endif()
endif()
