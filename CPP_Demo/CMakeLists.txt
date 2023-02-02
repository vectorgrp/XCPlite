cmake_minimum_required(VERSION 3.1.0)

project(CPP_Demo VERSION 5.0 LANGUAGES CXX)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CPP_Demo_SOURCES 
  main.cpp 
  ../src/xcpAppl.c ../src/xcp.cpp ../src/xcpLite.c ../src/xcpTl.c ../src/xcpServer.c ../src/A2L.cpp ../src/platform.c ../src/util.c
)

set(OPTION_DEBUG_LEVEL 2 CACHE STRING "Debug output level")
option(OPTION_ENABLE_TCP "Enable TCP" 1)
option(OPTION_USE_TCP "Use TCP by default" 0)
set(OPTION_SERVER_PORT 5555 CACHE STRING "XCP default port")
set(OPTION_SERVER_ADDR {0,0,0,0} CACHE STRING "XCP IP address to bind, ANY=0.0.0.0")
option(OPTION_ENABLE_A2L_GEN "Enable A2L file generator" 1)
configure_file(main_cfg.h.in ${PROJECT_SOURCE_DIR}/main_cfg.h)

add_executable(CPP_Demo ${CPP_Demo_SOURCES})
target_include_directories(CPP_Demo PUBLIC "${PROJECT_SOURCE_DIR}" )
target_include_directories(CPP_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../src" )
#set_target_properties(CPP_Demo PROPERTIES OUTPUT_NAME "CPP_Demo.out")
set_target_properties(CPP_Demo PROPERTIES SUFFIX ".out")

# compile*.c as C++
# add_compile_options("/TP")
file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/../*.c")
set_source_files_properties(${CFILES} PROPERTIES LANGUAGE CXX)


target_link_libraries(CPP_Demo PRIVATE m)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(CPP_Demo PRIVATE Threads::Threads)
