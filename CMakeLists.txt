# (C) 2018-2024 by Folkert van Heusden
# Released under MIT license

project(PDP-11)
cmake_minimum_required(VERSION 3.9)

add_compile_options(-Wall -pedantic -Wextra)

#add_compile_options(-fsanitize=undefined)
#add_link_options(-fsanitize=undefined)

#add_compile_options(-fanalyzer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FindPkgConfig)

set(APP_SOURCES
  breakpoint.cpp
  breakpoint_and.cpp
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
  console_ncurses.cpp
  console_posix.cpp
  cpu.cpp
  dc11.cpp
  debugger.cpp
  device.cpp
  disk_backend.cpp
  disk_backend_file.cpp
  disk_backend_nbd.cpp
  error.cpp
  kw11-l.cpp
  loaders.cpp
  log.cpp
  main.cpp
  memory.cpp
  mmu.cpp
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
)

add_executable(
  kek-codecoverage
  ${APP_SOURCES}
)

add_executable(
  kek-native
  ${APP_SOURCES}
)

target_compile_options(kek-native PRIVATE -march=native -mtune=native -O3)

target_compile_options(kek-codecoverage PRIVATE -fprofile-arcs -ftest-coverage)
target_link_options(kek-codecoverage PRIVATE -fprofile-arcs -ftest-coverage)

pkg_check_modules(NCURSES REQUIRED ncurses)
target_link_libraries(kek ${NCURSES_LIBRARIES})
target_include_directories(kek PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${NCURSES_CFLAGS_OTHER})
target_link_libraries(kek-codecoverage ${NCURSES_LIBRARIES})
target_include_directories(kek-codecoverage PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek-codecoverage PUBLIC ${NCURSES_CFLAGS_OTHER})
target_link_libraries(kek-native ${NCURSES_LIBRARIES})
target_include_directories(kek-native PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek-native PUBLIC ${NCURSES_CFLAGS_OTHER})

pkg_check_modules(PANEL REQUIRED panel)
target_link_libraries(kek ${PANEL_LIBRARIES})
target_include_directories(kek PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${PANEL_CFLAGS_OTHER})
target_link_libraries(kek-codecoverage ${PANEL_LIBRARIES})
target_include_directories(kek-codecoverage PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek-codecoverage PUBLIC ${PANEL_CFLAGS_OTHER})
target_link_libraries(kek-native ${PANEL_LIBRARIES})
target_include_directories(kek-native PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek-native PUBLIC ${PANEL_CFLAGS_OTHER})
endif (NOT WIN32)

if (WIN32)
add_executable(
  kek-win32
  ${APP_SOURCES}
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
find_package(Threads)
if (NOT WIN32)
target_link_libraries(kek Threads::Threads)
target_link_libraries(kek-codecoverage Threads::Threads)
else ()
target_link_libraries(kek-win32 Threads::Threads)

target_link_libraries(kek-win32 ws2_32)
endif ()

add_subdirectory(arduinojson)
target_link_libraries(kek ArduinoJson)
target_link_libraries(kek-codecoverage ArduinoJson)
target_link_libraries(kek-native ArduinoJson)

if (WIN32)
target_link_libraries(kek-win32 ArduinoJson)
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
