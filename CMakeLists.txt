project(PDP-11)
cmake_minimum_required(VERSION 3.9)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(
  kek
  bus.cpp
  cpu.cpp
  error.cpp
  main.cpp
  memory.cpp
  rk05.cpp
  rx02.cpp
  terminal.cpp
  tests.cpp
  tm-11.cpp
  tty.cpp
  utils.cpp
)

include(CheckIPOSupported)
check_ipo_supported(RESULT supported)

#set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(kek Threads::Threads)

include(FindPkgConfig)

pkg_check_modules(NCURSES REQUIRED ncurses)
target_link_libraries(kek ${NCURSES_LIBRARIES})
target_include_directories(kek PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${NCURSES_CFLAGS_OTHER})

pkg_check_modules(PANEL REQUIRED panel)
target_link_libraries(kek ${PANEL_LIBRARIES})
target_include_directories(kek PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${PANEL_CFLAGS_OTHER})
