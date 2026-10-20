cmake_minimum_required(VERSION 3.24)
project(p2boost VERSION 1.3.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "p2boost targets Portal 2 for Windows")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Portal 2 is 32-bit; configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(P2BOOST_INCLUDE_DIRECTORIES
  src/core
  src/engine
  src/features
  src/memory
  src/platform
  src/util
)

# Compile the readable HLSL source into a C header. Keeping shader bytecode out
# of source control makes the post-process implementation auditable.
file(GLOB P2BOOST_FXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x86/fxc.exe")
list(SORT P2BOOST_FXC_CANDIDATES ORDER DESCENDING)
list(LENGTH P2BOOST_FXC_CANDIDATES P2BOOST_FXC_COUNT)
if(P2BOOST_FXC_COUNT EQUAL 0)
  message(FATAL_ERROR "The x86 Windows SDK shader compiler (fxc.exe) is required")
endif()
list(GET P2BOOST_FXC_CANDIDATES 0 P2BOOST_FXC)

set(P2BOOST_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(P2BOOST_GAMMA_SHADER_HEADER "${P2BOOST_GENERATED_DIRECTORY}/monitor_gamma_shader.h")
add_custom_command(
  OUTPUT "${P2BOOST_GAMMA_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${P2BOOST_GENERATED_DIRECTORY}"
  COMMAND "${P2BOOST_FXC}" /nologo /T ps_2_0 /E main /Fh "${P2BOOST_GAMMA_SHADER_HEADER}"
          /Vn g_monitor_gamma_shader "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/monitor_gamma.hlsl"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/monitor_gamma.hlsl"
  VERBATIM
)

# The release library holds every feature and its runtime support; the DLL
# adds only the plugin entry point, console bridge, and NVIDIA profile code.
# Nothing under research/ is compiled.
add_library(p2boost_core STATIC
  src/features/flat_material_pass.cpp
  src/features/flat_texture_codec.cpp
  src/features/flat_texture_hooks.cpp
  src/features/flat_textures.cpp
  src/features/display_adjustments.cpp
  src/features/remote_view.cpp
  src/features/vgui_screen_cache.cpp
  src/features/queued_frames.cpp
  src/features/demo_provenance.cpp
  "${P2BOOST_GAMMA_SHADER_HEADER}"
  src/core/flashlight_buckets.cpp
  src/core/frame_limiter.cpp
  src/core/occlusion_skip.cpp
  src/core/vgui_screen_shadow_skip.cpp
  src/core/shadow_depth_reuse.cpp
  src/core/frame_metrics.cpp
  src/core/projected_shadow_cache.cpp
  src/core/profiler_overlay.cpp
  src/core/runtime_verification.cpp
  src/core/auto_update.cpp
  src/core/xinput_throttle.cpp
  src/engine/game_profile.cpp
  src/engine/portal2_context.cpp
  src/engine/portal_stories_mel.cpp
  src/memory/hook.cpp
  src/platform/game_window.cpp
  src/util/projected_frustum.cpp
)
target_include_directories(p2boost_core PUBLIC ${P2BOOST_INCLUDE_DIRECTORIES})
target_include_directories(p2boost_core PRIVATE "${P2BOOST_GENERATED_DIRECTORY}")
target_compile_definitions(p2boost_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX P2BOOST_VERSION="${PROJECT_VERSION}" _WIN32_WINNT=0x0601)
target_compile_options(p2boost_core PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /guard:cf)
target_link_libraries(p2boost_core PRIVATE bcrypt)

add_library(p2boost SHARED
  src/core/plugin.cpp
  src/engine/console.cpp
  src/engine/console_commands.cpp
  src/platform/nvidia_profile.cpp
)
target_include_directories(p2boost PRIVATE ${P2BOOST_INCLUDE_DIRECTORIES})
target_include_directories(p2boost SYSTEM PRIVATE third_party/nvapi/include)
target_compile_definitions(p2boost PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  P2BOOST_VERSION="${PROJECT_VERSION}"
)
target_compile_options(p2boost PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /guard:cf /external:W0)
target_link_options(p2boost PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/p2boost.def /guard:cf /DEBUG:FULL)
target_link_libraries(p2boost PRIVATE
  p2boost_core
  bcrypt
  shell32
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvapi/x86/nvapi.lib
)
set_target_properties(p2boost PROPERTIES
  OUTPUT_NAME p2boost
  PREFIX ""
  PDB_NAME p2boost
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

enable_testing()
add_executable(p2boost_tests tests/test_main.cpp)
target_include_directories(p2boost_tests PRIVATE ${P2BOOST_INCLUDE_DIRECTORIES})
target_compile_definitions(p2boost_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(p2boost_tests PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
target_link_libraries(p2boost_tests PRIVATE p2boost_core bcrypt)
add_test(NAME p2boost_tests COMMAND p2boost_tests)

