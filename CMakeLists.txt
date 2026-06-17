# (C) 2018-2026 by Folkert van Heusden
# Released under MIT license

project(PDP-11)
cmake_minimum_required(VERSION 3.10)

add_compile_options(-Wall -pedantic -Wextra)

#add_compile_options(-fsanitize=address,undefined)
#add_link_options(-fsanitize=address,undefined)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FindPkgConfig)

pkg_check_modules(SDL3 sdl3 sdl3-ttf)

if (TURBO EQUAL 1)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTURBO=1")
	MESSAGE("TURBO BUILD")
else()
	MESSAGE("normal build")
endif()

set(APP_IMGUI_SOURCES
	${CMAKE_SOURCE_DIR}/imgui/imgui.cpp
	${CMAKE_SOURCE_DIR}/imgui/imgui_draw.cpp
	${CMAKE_SOURCE_DIR}/imgui/imgui_tables.cpp
	${CMAKE_SOURCE_DIR}/imgui/imgui_widgets.cpp
	${CMAKE_SOURCE_DIR}/imgui/backends/imgui_impl_sdl3.cpp
	${CMAKE_SOURCE_DIR}/imgui/backends/imgui_impl_sdlrenderer3.cpp
)

if ((EXISTS "/usr/include/sys/timepps.h") AND (NOT WIN32))
	MESSAGE("PPS enabled")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWITH_PPS=1")
else ()
	MESSAGE("PPS disabled")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWITH_PPS=0")
endif ()

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
  comm_pst.cpp
  comm_tcp_socket_client.cpp
  comm_tcp_socket_server.cpp
  console.cpp
  console_comm.cpp
  console_imgui.cpp
  console_ncurses.cpp
  console_posix.cpp
  cpu.cpp
  dc11.cpp
  ddp.cpp
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

if (SDL3_FOUND)
	add_executable(
	  kek-native
	  ${APP_SOURCES}
	  ${APP_IMGUI_SOURCES}
	  main.cpp
	)
	add_executable(
	  kek
	  ${APP_SOURCES}
	  ${APP_IMGUI_SOURCES}
	  main.cpp
	)
	if (WIN32)
	add_executable(
	  kek-win32
	  ${APP_SOURCES}
	  ${APP_IMGUI_SOURCES}
	  main.cpp
	  win32.cpp
	)
	endif (WIN32)
else ()
	add_executable(
	  kek-native
	  ${APP_SOURCES}
	  main.cpp
	)
	add_executable(
	  kek
	  ${APP_SOURCES}
	  main.cpp
	)
	if (WIN32)
	add_executable(
	  kek-win32
	  ${APP_SOURCES}
	  main.cpp
	  win32.cpp
	)
	endif (WIN32)
endif ()

target_compile_options(kek-native PRIVATE -march=native -Ofast -fomit-frame-pointer)

if (WIN32)
add_executable(
  kek-win32
  ${APP_SOURCES}
  main.cpp
  win32.cpp
)
endif (WIN32)

if (WIN32)
	pkg_check_modules(PANEL REQUIRED panelw)
	target_link_libraries(kek-win32 ${PANEL_LIBRARIES})
	target_include_directories(kek-win32 PUBLIC ${PANEL_INCLUDE_DIRS})
	target_compile_options(kek-win32 PUBLIC ${PANEL_CFLAGS_OTHER})
else()
	pkg_check_modules(PANEL REQUIRED panel)
endif ()
target_link_libraries(kek ${PANEL_LIBRARIES})
target_include_directories(kek PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${PANEL_CFLAGS_OTHER})
target_link_libraries(kek-native PUBLIC ${PANEL_LIBRARIES})
target_include_directories(kek-native PUBLIC ${PANEL_INCLUDE_DIRS})
target_compile_options(kek-native PUBLIC ${PANEL_CFLAGS_OTHER})

if (WIN32)
	pkg_check_modules(NCURSES REQUIRED ncursesw)
	target_link_libraries(kek-win32 ${NCURSES_LIBRARIES})
	target_include_directories(kek-win32 PUBLIC ${NCURSES_INCLUDE_DIRS})
	target_compile_options(kek-win32 PUBLIC ${NCURSES_CFLAGS_OTHER})
else()
	pkg_check_modules(NCURSES REQUIRED ncurses)
endif ()
target_link_libraries(kek ${NCURSES_LIBRARIES})
target_include_directories(kek PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek PUBLIC ${NCURSES_CFLAGS_OTHER})
target_link_libraries(kek-native PUBLIC ${NCURSES_LIBRARIES})
target_include_directories(kek-native PUBLIC ${NCURSES_INCLUDE_DIRS})
target_compile_options(kek-native PUBLIC ${NCURSES_CFLAGS_OTHER})

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
find_package(Threads)
if (WIN32)
target_link_libraries(kek-win32 Threads::Threads)
target_link_libraries(kek-win32 ws2_32)
else ()
target_link_libraries(kek Threads::Threads)
target_link_libraries(kek-native PUBLIC Threads::Threads)
endif ()

add_subdirectory(arduinojson)
if (NOT WIN32)
	MESSAGE("With serialazation")
	target_link_libraries(kek ArduinoJson)
	target_link_libraries(kek-native PUBLIC ArduinoJson)
endif ()

if (SDL3_FOUND)
	MESSAGE("SDL3 found")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_IMGUI=1")
	target_link_libraries(kek ${SDL3_LIBRARIES})
	target_include_directories(kek PRIVATE ${CMAKE_SOURCE_DIR}/imgui)
	target_include_directories(kek PUBLIC ${SDL3_INCLUDE_DIRS})
	target_compile_options(kek PUBLIC ${SDL3_CFLAGS_OTHER})
	target_link_libraries(kek-native PUBLIC ${SDL3_LIBRARIES})
	target_include_directories(kek-native PRIVATE ${CMAKE_SOURCE_DIR}/imgui)
	target_include_directories(kek-native PUBLIC ${SDL3_INCLUDE_DIRS})
	target_compile_options(kek-native PUBLIC ${SDL3_CFLAGS_OTHER})
else ()
	MESSAGE("SDL3 NOT found")
endif ()

if (UNIX)
	target_compile_definitions(kek PRIVATE IS_POSIX=1)
	target_compile_definitions(kek-native PRIVATE IS_POSIX=1)
endif ()

if (UNIX)
include(FindPkgConfig)
pkg_check_modules(JANSSON REQUIRED jansson)

if (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
find_library(JANSSON_LIB jansson REQUIRED)
target_link_libraries(kek-native PRIVATE ${JANSSON_LIB})
target_link_libraries(kek ${JANSSON_LIB})
else ()
target_link_libraries(kek-native PRIVATE ${JANSSON_LIBRARIES})
target_link_libraries(kek ${JANSSON_LIBRARIES})
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
