# CPP_Demo
cmake_minimum_required(VERSION 3.1.0)

project(CPP_Demo VERSION 6.0 LANGUAGES CXX)
set(WINDOWS FALSE)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CPP_Demo_SOURCES 
  main.cpp 
  ../src/xcpAppl.c ../src/xcp.cpp ../src/xcpLite.c ../src/xcpEthTl.c ../src/xcpEthServer.c ../src/A2L.c ../src/A2Lpp.cpp ../src/platform.c ../src/util.c ../src/options.c
)

add_executable(CPP_Demo ${CPP_Demo_SOURCES})
target_include_directories(CPP_Demo PUBLIC "${PROJECT_SOURCE_DIR}" )
target_include_directories(CPP_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../src" )

# compile*.c as C++
file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/../*.c")
set_source_files_properties(${CFILES} PROPERTIES LANGUAGE CXX)

if (WINDOWS)
else ()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

#set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "CPP_Demo.out")
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".out")
target_link_libraries(${PROJECT_NAME} PRIVATE m)

endif ()
