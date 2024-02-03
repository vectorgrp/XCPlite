# XCPlite
cmake_minimum_required(VERSION 3.5)

set(MACOS TRUE)

if (MACOS)

  set(CMAKE_TOOLCHAIN_FILE "darwin-arm-toolchain.cmake")
  
  # Set the compilers explicitly (macOS brew ls gcc)
  # set(CMAKE_C_COMPILER /usr/local/Cellar/gcc/13.2.0/bin/x86_64-apple-darwin23-gcc-13)
  # set(CMAKE_CXX_COMPILER /usr/local/Cellar/gcc/13.2.0/bin/x86_64-apple-darwin23-gcc-13)

else ()

  set(CMAKE_C_COMPILER "gcc")
  set(CMAKE_CXX_COMPILER "g++")

endif ()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)


project(XCPlite VERSION 6.0 LANGUAGES C)

set(XCPlite_SOURCES main.c xcpAppl.c ../src/xcpLite.c ../src/xcpEthTl.c ../src/A2L.c ../src/platform.c )
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
