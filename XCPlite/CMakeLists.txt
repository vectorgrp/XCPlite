# XCPlite
cmake_minimum_required(VERSION 3.1.0)

project(XCPlite VERSION 6.0 LANGUAGES C)
set(WINDOWS FALSE)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(XCPlite 
  main.c xcpAppl.c
  ../src/xcpLite.c ../src/xcpEthTl.c ../src/A2L.c ../src/platform.c 
)
set_source_files_properties(${XCPlite_WIN_SOURCES} PROPERTIES LANGUAGE C)

set(XCPlite_LINUX_SOURCES 
  main.c xcpAppl.c ../src/xcpLite.c ../src/xcpEthTl.c ../src/A2L.c ../src/platform.c
)
set_source_files_properties(${XCPlite_LINUX_SOURCES} PROPERTIES LANGUAGE C)

if (WINDOWS)
add_executable(XCPlite ${XCPlite_WIN_SOURCES})
else ()
add_executable(XCPlite ${XCPlite_LINUX_SOURCES})
endif ()

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
