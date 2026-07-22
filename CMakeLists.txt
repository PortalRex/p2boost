cmake_minimum_required(VERSION 3.24)
project(p2boost VERSION 0.5.0 LANGUAGES CXX)

option(P2BOOST_RESEARCH_DRAW_TRACE "Instrument native D3D9 draw calls for research captures" OFF)

if(NOT WIN32)
  message(FATAL_ERROR "p2boost currently targets Portal 2 for Windows")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Portal 2 is 32-bit; configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(p2boost SHARED
  src/build_fingerprint.cpp
  src/console.cpp
  src/console_commands.cpp
  src/frame_pacer.cpp
  src/hook.cpp
  src/metrics.cpp
  src/plugin.cpp
  src/portal_trace.cpp
  src/renderer_trace.cpp
  src/shadow_trace.cpp
)
target_include_directories(p2boost PRIVATE src)
target_compile_definitions(p2boost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX P2BOOST_VERSION="${PROJECT_VERSION}")
if(P2BOOST_RESEARCH_DRAW_TRACE)
  target_compile_definitions(p2boost PRIVATE P2BOOST_RESEARCH_DRAW_TRACE=1)
endif()
target_compile_options(p2boost PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /guard:cf)
target_link_options(p2boost PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/p2boost.def /guard:cf /DEBUG:FULL)
target_link_libraries(p2boost PRIVATE bcrypt)
set_target_properties(p2boost PROPERTIES
  OUTPUT_NAME p2boost
  PREFIX ""
  PDB_NAME p2boost
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

enable_testing()
add_executable(p2boost_tests
  tests/test_main.cpp
  src/build_fingerprint.cpp
  src/frame_pacer.cpp
  src/hook.cpp
  src/metrics.cpp
  src/portal_trace.cpp
  src/renderer_trace.cpp
  src/shadow_trace.cpp
)
target_include_directories(p2boost_tests PRIVATE src)
target_compile_definitions(p2boost_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(p2boost_tests PRIVATE /W4 /WX /permissive-)
target_link_libraries(p2boost_tests PRIVATE bcrypt)
add_test(NAME p2boost_tests COMMAND p2boost_tests)

add_executable(p2profile tools/p2profile.cpp)
target_compile_definitions(p2profile PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(p2profile PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
target_link_options(p2profile PRIVATE /guard:cf)
target_link_libraries(p2profile PRIVATE dbghelp)
