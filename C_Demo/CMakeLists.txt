cmake_minimum_required(VERSION 3.1.0)

project(C_Demo VERSION 5.0 LANGUAGES C)

set(CMAKE_C_COMPILER "g++")
set(CMAKE_CXX_COMPILER "g++")

set(C_Demo_SOURCES 
  main.c xcpAppl.c ecu.c 
  ../src/xcpLite.c ../src/xcpTl.c ../src/xcpServer.c ../src/A2L.c ../src/platform.c ../src/util.c 
  ../xlapi/xl_udp.c ../xlapi/xl_pcap.c
)

set(OPTION_DEBUG_LEVEL 1 CACHE STRING "Debug output level")
option(OPTION_ENABLE_TCP "Use TCP" 1)
set(OPTION_SERVER_PORT 5555 CACHE STRING "XCP default port")
set(OPTION_SERVER_ADDR {127,0,0,1} CACHE STRING "XCP IP address")
option(OPTION_ENABLE_A2L_GEN "Enable A2L file generator" 1)

option(OPTION_ENABLE_CAL_SEGMENT "" 1)

option(OPTION_ENABLE_XLAPI_V3 "" 1)
set(OPTION_SERVER_XL_ADDR {192,168,0,200} CACHE STRING "")
set(OPTION_SERVER_XL_MAC {0xdc,0xa6,0x32,0x7e,0x66,0xdc} CACHE STRING "")
set(OPTION_SERVER_XL_NET "NET1" CACHE STRING "")
set(OPTION_SERVER_XL_SEG "SEG1" CACHE STRING "")



add_executable(C_Demo ${C_Demo_SOURCES})
target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}" )
target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../src" )
target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../xlapi" )
configure_file(main_cfg.h.in ${PROJECT_SOURCE_DIR}/main_cfg.h)


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(C_Demo PRIVATE Threads::Threads)
