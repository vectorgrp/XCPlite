cmake_minimum_required(VERSION 3.1.0)

project(C_Demo VERSION 5.0 LANGUAGES C)
set(WINDOWS TRUE)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

set(C_Demo_WIN_SOURCES 
  main.c ecu.c 
  ../src/xcpAppl.c ../src/xcpLite.c ../src/xcpTl.c ../src/xcpServer.c ../src/A2L.c ../src/platform.c ../src/util.c 
  ../xlapi/xl_udp.c ../xlapi/xl_pcap.c
)
set_source_files_properties(${C_Demo_WIN_SOURCES} PROPERTIES LANGUAGE C)

set(C_Demo_LINUX_SOURCES 
  main.c ecu.c 
  ../src/xcpAppl.c ../src/xcpLite.c ../src/xcpTl.c ../src/xcpServer.c ../src/A2L.c ../src/platform.c ../src/util.c 
)
set_source_files_properties(${C_Demo_LINUX_SOURCES} PROPERTIES LANGUAGE C)

set(OPTION_DEBUG_LEVEL 1 CACHE STRING "Debug output level")
option(OPTION_ENABLE_TCP "Enable TCP" 1)
option(OPTION_USE_TCP "Use TCP by default" 0)
set(OPTION_SERVER_PORT 5555 CACHE STRING "XCP default port")
set(OPTION_SERVER_ADDR {0,0,0,0} CACHE STRING "XCP IP address to bind, ANY=0.0.0.0")
option(OPTION_ENABLE_A2L_GEN "Enable A2L file generator" 1)
option(OPTION_ENABLE_CAL_SEGMENT "" 1)
option(OPTION_ENABLE_XLAPI_V3 "" 0)
set(OPTION_SERVER_XL_ADDR {192,168,0,200} CACHE STRING "")
set(OPTION_SERVER_XL_MAC {0xdc,0xa6,0x32,0x7e,0x66,0xdc} CACHE STRING "")
set(OPTION_SERVER_XL_NET "NET1" CACHE STRING "")
set(OPTION_SERVER_XL_SEG "SEG1" CACHE STRING "")
configure_file(main_cfg.h.in ${PROJECT_SOURCE_DIR}/main_cfg.h)

if (WINDOWS)
add_executable(C_Demo ${C_Demo_WIN_SOURCES})
else ()
add_executable(C_Demo ${C_Demo_LINUX_SOURCES})
endif ()

target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}" )
target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../src" )
target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../xlapi" )

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

if (WINDOWS)
else ()
#set_target_properties(C_Demo PROPERTIES OUTPUT_NAME "C_Demo.out")
set_target_properties(C_Demo PROPERTIES SUFFIX ".out")
target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif ()
