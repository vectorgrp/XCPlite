# C_Demo
cmake_minimum_required(VERSION 3.5)

set(MACOS FALSE) # Build for macOS Darwin ARM
set(WINDOWS FALSE) # Build for Windows x64
set(LINUX TRUE) # Build for Linus x64

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(C_Demo VERSION 6.0 LANGUAGES C)

set(C_Demo_SOURCES 
	main.c ecu.c 
	../src/xcpAppl.c ../src/xcpLite.c ../src/xcpEthServer.c ../src/xcpEthTl.c ../src/A2L.c ../src/platform.c  ../src/util.c ../src/options.c )

# compile as C
set_source_files_properties(${C_Demo_SOURCES} PROPERTIES LANGUAGE C)

add_executable(C_Demo ${C_Demo_SOURCES})
target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}" )
target_include_directories(C_Demo PUBLIC "${PROJECT_SOURCE_DIR}/../src" )

if (WINDOWS)

else ()

  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

  set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".out")
  target_link_libraries(${PROJECT_NAME} PRIVATE m)

endif ()
