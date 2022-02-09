cmake_minimum_required(VERSION 3.1.0)

project(CPP_Demo VERSION 5.0 LANGUAGES CXX)

set(CMAKE_C_COMPILER "g++")
set(CMAKE_CXX_COMPILER "g++")

set(CPP_Demo_SOURCES 
  main.cpp xcpAppl.c 
  ../src/xcp.cpp ../src/xcpLite.c ../src/xcpTl.c ../src/xcpServer.c ../src/A2L.cpp ../src/platform.c ../src/util.c
)

set(OPTION_DEBUG_LEVEL 1 CACHE STRING "Debug output level")
option(OPTION_USE_TCP "Use TCP" 1)
set(OPTION_SERVER_PORT 5555 CACHE STRING "XCP default port")
set(OPTION_SERVER_ADDR {127,0,0,1} CACHE STRING "XCP IP address")
option(OPTION_ENABLE_A2L_GEN "Enable A2L file generator" 1)

add_executable(CPP_Demo ${CPP_Demo_SOURCES})
target_include_directories(CPP_Demo PUBLIC "${PROJECT_SOURCE_DIR}" )
target_include_directories(CPP_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../src" )
configure_file(main_cfg.h.in ${PROJECT_SOURCE_DIR}/main_cfg.h)

# compile*.c as C++
file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/../*.c")
set_source_files_properties(${CFILES} PROPERTIES LANGUAGE CXX)


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(CPP_Demo PRIVATE Threads::Threads)
