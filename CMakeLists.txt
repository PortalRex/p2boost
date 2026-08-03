cmake_minimum_required(VERSION 3.24)
project(p2boost VERSION 0.8.4 LANGUAGES CXX)

option(P2BOOST_BUILD_RESEARCH_TOOLS "Build offline profiling tools from research/" OFF)

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

# The release library contains the two benchmark-proven fixes, the optional
# flat-texture mode, and their shared runtime support. Historical probes stay
# under research/ and are never compiled into p2boost.dll.
add_library(p2boost_core STATIC
  src/features/flat_textures.cpp
  src/features/display_adjustments.cpp
  src/features/demo_provenance.cpp
  "${P2BOOST_GAMMA_SHADER_HEADER}"
  src/core/frame_limiter.cpp
  src/core/frame_metrics.cpp
  src/core/projected_shadow_cache.cpp
  src/core/profiler_overlay.cpp
  src/core/runtime_verification.cpp
  src/memory/hook.cpp
)
target_include_directories(p2boost_core PUBLIC ${P2BOOST_INCLUDE_DIRECTORIES})
target_include_directories(p2boost_core PRIVATE "${P2BOOST_GENERATED_DIRECTORY}")
target_compile_definitions(p2boost_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
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

if(P2BOOST_BUILD_RESEARCH_TOOLS)
  # Compile the archived hook primitives separately so they can never leak
  # into the production static library or DLL.
  add_library(p2boost_research_hooks STATIC
    research/runtime/native_d3d9/research_hooks.cpp
  )
  target_include_directories(p2boost_research_hooks PRIVATE
    ${P2BOOST_INCLUDE_DIRECTORIES}
    research/runtime/native_d3d9
  )
  target_compile_definitions(p2boost_research_hooks PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(p2boost_research_hooks PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)

  add_executable(p2profile research/tools/p2profile.cpp)
  target_include_directories(p2profile PRIVATE ${P2BOOST_INCLUDE_DIRECTORIES})
  target_compile_definitions(p2profile PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(p2profile PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
  target_link_options(p2profile PRIVATE /guard:cf)
  target_link_libraries(p2profile PRIVATE dbghelp)
endif()
