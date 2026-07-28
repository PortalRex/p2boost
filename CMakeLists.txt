cmake_minimum_required(VERSION 3.24)
project(p2boost VERSION 0.5.0 LANGUAGES CXX)

option(P2BOOST_RESEARCH_DRAW_TRACE "Instrument native D3D9 draw calls for research captures" OFF)
option(P2BOOST_RESEARCH_WORLD_TRACE "Instrument BSP world-surface construction for research captures" OFF)
option(P2BOOST_RESEARCH_VISIBILITY_TRACE "Instrument only engine/client visibility construction" OFF)
option(P2BOOST_RESEARCH_PVS_TRACE "Instrument compressed PVS expansion" OFF)
option(P2BOOST_RESEARCH_RENDERABLE_TRACE "Instrument client renderable-list construction" OFF)

if(NOT WIN32)
  message(FATAL_ERROR "p2boost currently targets Portal 2 for Windows")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Portal 2 is 32-bit; configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(P2BOOST_CORE_SOURCES
  src/build_fingerprint.cpp
  src/activation_trace.cpp
  src/frame_pacer.cpp
  src/hook.cpp
  src/metrics.cpp
  src/portal_trace.cpp
  src/profiler_overlay.cpp
  src/renderer_research.cpp
  src/renderer_trace.cpp
  src/shadow_trace.cpp
  src/server_trace.cpp
  src/world_trace.cpp
)

# Compile the implementation once, then link the same objects into the plugin
# and its tests. This keeps CI fast and prevents test-only build differences.
add_library(p2boost_core STATIC ${P2BOOST_CORE_SOURCES})
target_include_directories(p2boost_core PUBLIC src)
target_compile_definitions(p2boost_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(p2boost_core PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /guard:cf)
if(P2BOOST_RESEARCH_DRAW_TRACE)
  target_compile_definitions(p2boost_core PUBLIC P2BOOST_RESEARCH_DRAW_TRACE=1)
endif()
if(P2BOOST_RESEARCH_WORLD_TRACE)
  # Plugin startup is compiled in the consuming DLL target and must see this
  # flag so it actually installs the research-only core hooks.
  target_compile_definitions(p2boost_core PUBLIC P2BOOST_RESEARCH_WORLD_TRACE=1
    P2BOOST_RESEARCH_VISIBILITY_ANY=1 P2BOOST_RESEARCH_VISIBILITY_METRICS=1)
endif()
if(P2BOOST_RESEARCH_VISIBILITY_TRACE)
  target_compile_definitions(p2boost_core PUBLIC P2BOOST_RESEARCH_VISIBILITY_TRACE=1
    P2BOOST_RESEARCH_VISIBILITY_ANY=1 P2BOOST_RESEARCH_VISIBILITY_METRICS=1)
endif()
if(P2BOOST_RESEARCH_PVS_TRACE)
  target_compile_definitions(p2boost_core PUBLIC P2BOOST_RESEARCH_PVS_TRACE=1
    P2BOOST_RESEARCH_VISIBILITY_ANY=1 P2BOOST_RESEARCH_VISIBILITY_METRICS=1)
endif()
if(P2BOOST_RESEARCH_RENDERABLE_TRACE)
  target_compile_definitions(p2boost_core PUBLIC P2BOOST_RESEARCH_RENDERABLE_TRACE=1
    P2BOOST_RESEARCH_VISIBILITY_ANY=1 P2BOOST_RESEARCH_VISIBILITY_METRICS=1)
endif()

add_library(p2boost SHARED
  src/console.cpp
  src/console_commands.cpp
  src/plugin.cpp
)
target_include_directories(p2boost PRIVATE src)
target_compile_definitions(p2boost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX P2BOOST_VERSION="${PROJECT_VERSION}")
target_compile_options(p2boost PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /guard:cf)
target_link_options(p2boost PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/p2boost.def /guard:cf /DEBUG:FULL)
target_link_libraries(p2boost PRIVATE p2boost_core bcrypt)
set_target_properties(p2boost PROPERTIES
  OUTPUT_NAME p2boost
  PREFIX ""
  PDB_NAME p2boost
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

enable_testing()
add_executable(p2boost_tests
  tests/test_main.cpp
)
target_include_directories(p2boost_tests PRIVATE src)
target_compile_definitions(p2boost_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(p2boost_tests PRIVATE /W4 /WX /permissive-)
target_link_libraries(p2boost_tests PRIVATE p2boost_core bcrypt)
add_test(NAME p2boost_tests COMMAND p2boost_tests)

add_executable(p2profile tools/p2profile.cpp)
target_include_directories(p2profile PRIVATE src)
target_compile_definitions(p2profile PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(p2profile PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
target_link_options(p2profile PRIVATE /guard:cf)
target_link_libraries(p2profile PRIVATE dbghelp)
