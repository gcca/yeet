cmake_minimum_required(VERSION 4.3)

project(yeet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER
      ${CMAKE_COMMAND}
      -E
      env
      CCACHE_SLOPPINESS=pch_defines,time_macros
      "CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}"
      "${CCACHE_PROGRAM}")
endif()

option(YEET_TEST "Build the test executable" OFF)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
  "#include <spawn.h>
   int main() {
     posix_spawn_file_actions_t actions;
     posix_spawn_file_actions_init(&actions);
     return posix_spawn_file_actions_addchdir(&actions, \"/\");
   }"
  HAVE_SPAWN_ADDCHDIR_POSIX)

check_cxx_source_compiles(
  "#include <spawn.h>
   int main() {
     posix_spawn_file_actions_t actions;
     posix_spawn_file_actions_init(&actions);
     return posix_spawn_file_actions_addchdir_np(&actions, \"/\");
   }"
  HAVE_SPAWN_ADDCHDIR_NP)

if(HAVE_SPAWN_ADDCHDIR_POSIX OR HAVE_SPAWN_ADDCHDIR_NP)
  set(HAVE_SPAWN_ADDCHDIR ON)
endif()

function(yeet_spawn_definitions target)
  if(HAVE_SPAWN_ADDCHDIR)
    target_compile_definitions(${target} PRIVATE HAVE_SPAWN_ADDCHDIR)
  endif()
  if(HAVE_SPAWN_ADDCHDIR_POSIX)
    target_compile_definitions(${target} PRIVATE HAVE_SPAWN_ADDCHDIR_POSIX)
  endif()
endfunction()

set(YEET_SOURCES
    src/yeet/conf.cpp
    src/yeet/db.cpp
    src/yeet/exec/capture.cpp
    src/yeet/exec/collect.cpp
    src/yeet/exec/spawn.cpp
    src/yeet/log.cpp
    src/yeet/loop.cpp
    src/yeet/schedule/plan.cpp
    src/yeet/schedule/spec.cpp
    src/yeet/store.cpp
    src/yeet/timefmt.cpp)

add_executable(yeet src/yeet.cc ${YEET_SOURCES})

target_include_directories(yeet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(yeet SYSTEM
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
target_link_libraries(yeet PRIVATE sqlite3)

yeet_spawn_definitions(yeet)

if(YEET_TEST)
  find_package(GTest REQUIRED)

  enable_testing()

  add_executable(
    yeet-test
    ${YEET_SOURCES}
    src/yeet/conf-test.cc
    src/yeet/exec/capture-test.cc
    src/yeet/exec/collect-test.cc
    src/yeet/log-test.cc
    src/yeet/schedule/plan-test.cc
    src/yeet/schedule/spec-test.cc
    src/yeet/store-test.cc
    src/yeet/timefmt-test.cc)

  target_include_directories(yeet-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_include_directories(yeet-test SYSTEM
                             PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
  target_link_libraries(yeet-test PRIVATE GTest::gtest_main sqlite3)

  yeet_spawn_definitions(yeet-test)

  # The schema is authored by the OCaml yeet-initdb and dumped with
  # --print-schema. Loading that artifact here keeps the DDL single-authored
  # instead of copied into the tests.
  target_compile_definitions(
    yeet-test PRIVATE
    YEET_SCHEMA_SQL="${CMAKE_CURRENT_SOURCE_DIR}/db/schema.sql")

  include(GoogleTest)
  gtest_discover_tests(yeet-test)
endif()
