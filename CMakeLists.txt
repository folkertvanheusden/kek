# (C) 2018-2024 by Folkert van Heusden
# Released under MIT license

project(PDP-11)
cmake_minimum_required(VERSION 3.9)

add_compile_options(-Wall -pedantic -Wextra)

#add_compile_options(-fsanitize=address,undefined)
#add_link_options(-fsanitize=address,undefined)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FindPkgConfig)

if (TURBO EQUAL 1)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTURBO=1")
	MESSAGE("TURBO BUILD")
else()
	MESSAGE("normal build")
endif()

set(APP_SOURCES
  benchmark.cpp
  blinkenlights.cpp
  breakpoint.cpp
  breakpoint_and.cpp
  breakpoint_instruction.cpp
  breakpoint_memory.cpp
  breakpoint_or.cpp
  breakpoint_parser.cpp
  breakpoint_register.cpp
  bus.cpp
  comm.cpp
  comm_posix_tty.cpp
  comm_tcp_socket_client.cpp
  comm_tcp_socket_server.cpp
  console.cpp
  console_comm.cpp
  console_ncurses.cpp
  console_posix.cpp
  cpu.cpp
  dc11.cpp
  debugger.cpp
  deqna.cpp
  device.cpp
  disk_backend.cpp
  disk_backend_file.cpp
  disk_backend_nbd.cpp
  dz11.cpp
  error.cpp
  eth_transport.cpp
  eth_transport_linux.cpp
  eth_transport_vxlan.cpp
  kw11-l.cpp
  loaders.cpp
  log.cpp
  memory.cpp
  mmu.cpp
  my_lock.cpp
  rk05.cpp
  rl02.cpp
  rp06.cpp
  terminal.cpp
  tm-11.cpp
  tty.cpp
  utils.cpp
)

if (NOT WIN32)
add_executable(
  kek
  ${APP_SOURCES}
  main.cpp
)

add_executable(
  kek-codecoverage
  ${APP_SOURCES}
  main.cpp
)

add_executable(
  kek-native
  ${APP_SOURCES}
  main.cpp
)

###
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

SET(LINKING_STUFF
  GTest::gtest_main
  Threads::Threads
)

add_executable(
  dz11-test
  ${APP_SOURCES}
)

target_link_libraries(
  dz11-test
  ${LINKING_STUFF}
)
target_compile_options(dz11-test PRIVATE -O1 -fno-inline -DUNIT_TEST)
###

target_compile_options(kek-native PRIVATE -march=native -Ofast -fomit-frame-pointer)

target_compile_options(kek-codecoverage PRIVATE -fprofile-arcs -ftest-coverage)
target_link_options(kek-codecoverage PRIVATE -fprofile-arcs -ftest-coverage)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
pkg_check_modules(NCURSES REQUIRED ncursesw)
else ()
pkg_check_modules(NCURSES REQUIRED ncurses)
endif ()
target_link_libraries(kek ${NCURSES_LIBRARIES})
target_include_directories(kek PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${NCURSES_CFLAGS_OTHER})
target_link_libraries(kek-codecoverage ${NCURSES_LIBRARIES})
target_include_directories(kek-codecoverage PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek-codecoverage PUBLIC ${NCURSES_CFLAGS_OTHER})
target_link_libraries(kek-native PUBLIC ${NCURSES_LIBRARIES})
target_include_directories(kek-native PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek-native PUBLIC ${NCURSES_CFLAGS_OTHER})
target_link_libraries(dz11-test ${NCURSES_LIBRARIES})
target_include_directories(dz11-test PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(dz11-test PUBLIC ${NCURSES_CFLAGS_OTHER})

if (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
pkg_check_modules(PANEL REQUIRED panelw)
else ()
pkg_check_modules(PANEL REQUIRED panel)
endif ()
target_link_libraries(kek ${PANEL_LIBRARIES})
target_include_directories(kek PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${PANEL_CFLAGS_OTHER})
target_link_libraries(kek-codecoverage ${PANEL_LIBRARIES})
target_include_directories(kek-codecoverage PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek-codecoverage PUBLIC ${PANEL_CFLAGS_OTHER})
target_link_libraries(kek-native PUBLIC ${PANEL_LIBRARIES})
target_include_directories(kek-native PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek-native PUBLIC ${PANEL_CFLAGS_OTHER})
target_link_libraries(dz11-test ${PANEL_LIBRARIES})
target_include_directories(dz11-test PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(dz11-test PUBLIC ${PANEL_CFLAGS_OTHER})
endif (NOT WIN32)

if (WIN32)
add_executable(
  kek-win32
  ${APP_SOURCES}
  main.cpp
  win32.cpp
)
endif (WIN32)

include(CheckIPOSupported)
check_ipo_supported(RESULT supported)
set(CMAKE_BUILD_TYPE RelWithDebInfo)
cmake_policy(SET CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
#set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
set(THREADS_PREFER_PTHREAD_FLAG ON)
#if (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
#target_link_libraries(kek-native PUBLIC pthreads)
#target_link_libraries(kek PUBLIC pthreads)
#endif ()
find_package(Threads)
if (WIN32)
target_link_libraries(kek-win32 Threads::Threads)
target_link_libraries(kek-win32 ws2_32)
else ()
target_link_libraries(kek Threads::Threads)
target_link_libraries(kek-native PUBLIC Threads::Threads)
target_link_libraries(kek-codecoverage Threads::Threads)
endif ()

add_subdirectory(arduinojson)
if (WIN32)
target_link_libraries(kek-win32 ArduinoJson)
else()
target_link_libraries(kek ArduinoJson)
target_link_libraries(kek-native PUBLIC ArduinoJson)
target_link_libraries(kek-codecoverage ArduinoJson)
target_link_libraries(dz11-test ArduinoJson)
endif ()

if (UNIX)
include(FindPkgConfig)
pkg_check_modules(JANSSON REQUIRED jansson)

if (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
find_library(JANSSON_LIB jansson REQUIRED)
target_link_libraries(kek-native PRIVATE ${JANSSON_LIB})
else ()
target_link_libraries(kek-native PRIVATE ${JANSSON_LIBRARIES})
endif ()

target_include_directories(kek-native PUBLIC ${JANSSON_INCLUDE_DIRS})
target_compile_options(kek-native PUBLIC ${JANSSON_CFLAGS_OTHER} -DJANSSON)
endif ()

add_custom_target(coverage
    COMMAND ${CMAKE_COMMAND} -E remove_directory coverage
    COMMAND ${CMAKE_COMMAND} -E make_directory coverage
    COMMAND lcov --capture --directory . --output-file coverage.info
    COMMAND genhtml coverage.info --output-directory coverage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_custom_target(coverage_reset
    COMMAND lcov --directory . --zerocounters
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
