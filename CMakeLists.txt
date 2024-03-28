# (C) 2018-2024 by Folkert van Heusden
# Released under MIT license

project(PDP-11)
cmake_minimum_required(VERSION 3.9)

add_compile_options(-Wall -pedantic -Wextra)

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (NOT WIN32)

add_executable(
  kek
  bus.cpp
  console.cpp
  console_ncurses.cpp
  console_posix.cpp
  cpu.cpp
  debugger.cpp
  disk_backend.cpp
  disk_backend_file.cpp
  disk_backend_nbd.cpp
  error.cpp
  kw11-l.cpp
  loaders.cpp
  log.cpp
  main.cpp
  memory.cpp
  rk05.cpp
  rl02.cpp
  terminal.cpp
  tm-11.cpp
  tty.cpp
  utils.cpp
)

endif (NOT WIN32)

if (WIN32)

add_executable(
  kek-win32
  bus.cpp
  console.cpp
  console_posix.cpp
  cpu.cpp
  debugger.cpp
  disk_backend.cpp
  disk_backend_file.cpp
  disk_backend_nbd.cpp
  error.cpp
  kw11-l.cpp
  loaders.cpp
  log.cpp
  main.cpp
  memory.cpp
  rk05.cpp
  rl02.cpp
  tm-11.cpp
  tty.cpp
  utils.cpp
  win32.cpp
)

endif (WIN32)

include(CheckIPOSupported)
check_ipo_supported(RESULT supported)
set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
#set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
if (NOT WIN32)
target_link_libraries(kek Threads::Threads)
else ()
target_link_libraries(kek-win32 Threads::Threads)

target_link_libraries(kek-win32 ws2_32)
endif ()

include(FindPkgConfig)

pkg_check_modules(NCURSES REQUIRED ncurses)
target_link_libraries(kek ${NCURSES_LIBRARIES})
target_include_directories(kek PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${NCURSES_CFLAGS_OTHER})

pkg_check_modules(PANEL REQUIRED panel)
target_link_libraries(kek ${PANEL_LIBRARIES})
target_include_directories(kek PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${PANEL_CFLAGS_OTHER})

pkg_check_modules(JANSSON REQUIRED jansson)
target_link_libraries(kek ${JANSSON_LIBRARIES})
target_include_directories(kek PUBLIC ${JANSSON_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${JANSSON_CFLAGS_OTHER})
