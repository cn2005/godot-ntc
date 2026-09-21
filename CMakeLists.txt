cmake_minimum_required(VERSION 3.17)

set(GODOTCPP_CUSTOM_API_FILE "${CMAKE_CURRENT_SOURCE_DIR}/extension_api.json" CACHE FILEPATH "")
set(GODOTCPP_TARGET "template_debug" CACHE STRING "")

project(ntc_godot LANGUAGES CXX)

add_subdirectory(thirdparty/godot-cpp)

include(FetchContent)
FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG vulkan-sdk-1.4.321.0
)
FetchContent_MakeAvailable(VulkanHeaders)

set(NTC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "NVIDIA RTX NTC SDK root (libraries/RTXNTC-Library and bin/windows-x64)")
set(LIBNTC_LIB "${NTC_ROOT}/bin/windows-x64/libntc.lib")
set(LIBNTC_DLL "${NTC_ROOT}/bin/windows-x64/libntc.dll")

add_library(ntc_godot SHARED
    src/register_types.cpp
    src/ntc_runtime.cpp
    src/ntc_texture_set.cpp
    src/ntc_vk.cpp
    src/ntc_decode.cpp
    src/ntc_compress.cpp
    src/ntc_material.cpp
)

target_include_directories(ntc_godot PRIVATE
    src
    "${NTC_ROOT}/libraries/RTXNTC-Library/include"
)

target_link_libraries(ntc_godot PRIVATE
    godot-cpp
    Vulkan::Headers
    "${LIBNTC_LIB}"
    delayimp
)

target_compile_features(ntc_godot PRIVATE cxx_std_17)
target_compile_definitions(ntc_godot PRIVATE VK_NO_PROTOTYPES)

if(MSVC)
    target_compile_options(ntc_godot PRIVATE /W3 /wd4244 /wd4267)
    target_link_options(ntc_godot PRIVATE /DELAYLOAD:libntc.dll)
    # Match godot-cpp (GODOTCPP_USE_STATIC_CPP defaults to ON).
    set_property(TARGET ntc_godot PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(_out_dir "${CMAKE_CURRENT_SOURCE_DIR}/demo/bin/windows")
set_target_properties(ntc_godot PROPERTIES
    OUTPUT_NAME "libntc_godot"
    RUNTIME_OUTPUT_DIRECTORY "${_out_dir}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_out_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_out_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_out_dir}"
    LIBRARY_OUTPUT_DIRECTORY "${_out_dir}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${_out_dir}"
    PDB_OUTPUT_DIRECTORY "${_out_dir}"
)

add_custom_command(TARGET ntc_godot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBNTC_DLL}" "${_out_dir}/libntc.dll"
    COMMENT "Copy libntc.dll next to the GDExtension"
)
