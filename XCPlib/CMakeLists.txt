# XCPlite
cmake_minimum_required(VERSION 3.5)

set(MACOS FALSE) # Build for macOS Darwin ARM
set(WINDOWS FALSE) # Build for Windows x64
set(LINUX TRUE) # Build for Linus x64

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(XCPlite VERSION 6.0 LANGUAGES C CXX)

set(XCPlib_SOURCES xcpAppl.c ../src/xcpLite.c ../src/xcpEthTl.c ../src/xcpEthServer.c ../src/A2L.c ../src/platform.c )
set(XCPlite_SOURCES main.c  )

# compile as C
# set_source_files_properties(${XCPlite_SOURCES} PROPERTIES LANGUAGE C)
# set_source_files_properties(${XCPlib_SOURCES} PROPERTIES LANGUAGE C)

if (WINDOWS)

  add_executable(XCPlite ${XCPlite_SOURCES} ${XCPlib_SOURCES})
  target_include_directories(XCPlite PUBLIC "${PROJECT_SOURCE_DIR}" )
  target_include_directories(XCPlite PUBLIC "${PROJECT_SOURCE_DIR}/../src" )
  
else ()

  add_library(XCP STATIC ${XCPlib_SOURCES} )
  target_include_directories(XCP PUBLIC "${PROJECT_SOURCE_DIR}" )
  target_include_directories(XCP PUBLIC "${PROJECT_SOURCE_DIR}/../src" )

  add_executable(XCPlite ${XCPlite_SOURCES})
  target_include_directories(XCPlite PUBLIC "${PROJECT_SOURCE_DIR}" )
  target_include_directories(XCPlite PUBLIC "${PROJECT_SOURCE_DIR}/../src" )
  set_target_properties(XCPlite PROPERTIES SUFFIX ".out")
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  target_link_libraries(XCPlite PRIVATE Threads::Threads)
  target_link_libraries(XCPlite PRIVATE m)
  target_link_libraries(XCPlite PRIVATE XCP)

endif ()

