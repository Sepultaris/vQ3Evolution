cmake_minimum_required(VERSION 3.22)
project(VQ3ESoftwareDenoiser LANGUAGES CXX)
# Deliberately separate from the GPL engine build and distribution. This target
# consumes a user's local NVIDIA SDK build; it never downloads/relicenses it.
set(NRD_SOURCE "" CACHE PATH "Pinned NRD source checkout")
set(NRD_BUILD "" CACHE PATH "NRD build directory")
find_package(Vulkan REQUIRED)
find_program(DXC dxc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
foreach(entry Prepare Compose)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${entry}.h"
        COMMAND "${DXC}" -spirv -fspv-target-env=vulkan1.2 -T cs_6_0 -E ${entry}
            -I "${NRD_SOURCE}/Shaders" -Fh "${CMAKE_CURRENT_BINARY_DIR}/${entry}.h"
            -Vn vqe_nrd_${entry} "${CMAKE_CURRENT_SOURCE_DIR}/pack.hlsl"
        DEPENDS pack.hlsl "${NRD_SOURCE}/Shaders/NRD.hlsli" VERBATIM)
endforeach()
add_library(vq3e_nrd SHARED adapter.cpp "${CMAKE_CURRENT_BINARY_DIR}/Prepare.h" "${CMAKE_CURRENT_BINARY_DIR}/Compose.h")
target_compile_features(vq3e_nrd PRIVATE cxx_std_17)
target_compile_definitions(vq3e_nrd PRIVATE VK_NO_PROTOTYPES NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(vq3e_nrd PRIVATE "${NRD_SOURCE}/Include" "${CMAKE_CURRENT_BINARY_DIR}" ${Vulkan_INCLUDE_DIRS})
target_link_libraries(vq3e_nrd PRIVATE
    "${NRD_SOURCE}/_Bin/Release/NRD.lib"
    "${NRD_BUILD}/_deps/shadermake-build/Release/ShaderMakeBlob.lib")
set_target_properties(vq3e_nrd PROPERTIES PREFIX "")
add_executable(software_denoise_check ../../../tests/software_denoise_check.cpp)
target_compile_features(software_denoise_check PRIVATE cxx_std_17)
target_include_directories(software_denoise_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(software_denoise_check PRIVATE vq3e_nrd Vulkan::Vulkan)
