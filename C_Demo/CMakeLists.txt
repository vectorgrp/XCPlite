# C_Demo
cmake_minimum_required(VERSION 3.1.0)

project(C_Demo VERSION 6.0 LANGUAGES C)
set(WINDOWS FALSE)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

set(C_Demo_WIN_SOURCES 
  main.c ecu.c 
  ../src/xcpAppl.c ../src/xcpLite.c ../src/xcpTl.c ../src/xcpServer.c ../src/A2L.c ../src/platform.c ../src/util.c ../src/options.c 
)
set_source_files_properties(${C_Demo_WIN_SOURCES} PROPERTIES LANGUAGE C)

set(C_Demo_LINUX_SOURCES 
  main.c ecu.c 
  ../src/xcpAppl.c ../src/xcpLite.c ../src/xcpTl.c ../src/xcpServer.c ../src/A2L.c ../src/platform.c ../src/util.c ../src/options.c
)
set_source_files_properties(${C_Demo_LINUX_SOURCES} PROPERTIES LANGUAGE C)

set(OPTION_DEBUG_LEVEL 2 CACHE STRING "Debug output level")

option(OPTION_ENABLE_TCP "Enable TCP" 1)
option(OPTION_USE_TCP "Use TCP by default" 0)
set(OPTION_SERVER_PORT 5555 CACHE STRING "XCP default port")
set(OPTION_SERVER_ADDR {0,0,0,0} CACHE STRING "XCP IP address to bind, ANY=0.0.0.0")

configure_file(main_cfg.h.in ${PROJECT_SOURCE_DIR}/main_cfg.h)

if (WINDOWS)
add_executable(C_Demo ${C_Demo_WIN_SOURCES})
else ()
add_executable(C_Demo ${C_Demo_LINUX_SOURCES})
endif ()

target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}" )
target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../src" )


if (WINDOWS)
else ()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

#set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "C_Demo.out")
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".out")
target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif ()
