# XCPlite
cmake_minimum_required(VERSION 3.5)

set(MACOS FALSE) # Build for macOS Darwin ARM
set(WINDOWS FALSE) # Build for Windows x64
set(LINUX TRUE) # Build for Linus x64

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(XCPlite VERSION 6.0 LANGUAGES C)

set(XCPlite_SOURCES 
	main.c xcpAppl.c 
	../src/xcpLite.c ../src/xcpEthTl.c ../src/A2L.c ../src/platform.c )

# compile as C
set_source_files_properties(${XCPlite_SOURCES} PROPERTIES LANGUAGE C)

add_executable(XCPlite ${XCPlite_SOURCES})
target_include_directories(XCPlite PUBLIC "${PROJECT_SOURCE_DIR}" )
target_include_directories(XCPlite PUBLIC "${PROJECT_SOURCE_DIR}/../src" )

if (WINDOWS)

else ()

  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

  set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".out")
  target_link_libraries(${PROJECT_NAME} PRIVATE m)

endif ()
