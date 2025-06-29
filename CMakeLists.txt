# c_demo

cmake_minimum_required(VERSION 3.5)

project(xcplib VERSION 1.0.0 LANGUAGES C)



if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message("Building for 64-bit platform")
    set(PLATFORM_64BIT TRUE)
else()
    message("Building for 32-bit platform")
    set(PLATFORM_32BIT TRUE)
endif()


if(WIN32)
   message("Building for WINDOWS")
   set(WINDOWS FALSE) # Build for Windows
elseif(APPLE)
   message("Building for APPLE")
   set(MACOS TRUE) # Build for macOS Darwin ARM
elseif(UNIX)
    message("Building for UNIX")
    set(LINUX FALSE) # Build for Linus x64
endif()


set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR})
message(STATUS "PROJECT_ROOT is: ${PROJECT_ROOT}")

# xcplib
if(PLATFORM_64BIT)
set(xcplib_SOURCES src/xcpAppl.c src/xcpLite.c src/xcpEthServer.c src/xcpEthTl.c src/xcpQueue64.c src/a2l.c src/platform.c   )   
else()
set(xcplib_SOURCES src/xcpAppl.c src/xcpLite.c src/xcpEthServer.c src/xcpEthTl.c  src/xcpQueue32.c src/a2l.c src/platform.c   )   
endif()


set_source_files_properties(${xcplib_SOURCES} PROPERTIES LANGUAGE C)
add_library(xcplib ${xcplib_SOURCES})
target_include_directories(xcplib PUBLIC "${PROJECT_ROOT}/src" )
target_compile_options(xcplib
    PRIVATE
        -Wall
        -Wextra
        -Wconversion
#        -Werror
        -pedantic
#        -Wshadow
)

# Example hello_xcp
set(hello_xcp_SOURCES examples/hello_xcp/src/main.c ${xcplib_SOURCES} )
add_executable(hello_xcp ${hello_xcp_SOURCES})
target_include_directories(hello_xcp PUBLIC "${PROJECT_ROOT}" )
target_link_libraries( hello_xcp PRIVATE xcplib)

# Example c_demo
set(c_demo_SOURCES examples/c_demo/src/main.c ${xcplib_SOURCES} )
add_executable(c_demo ${c_demo_SOURCES})
target_include_directories(c_demo PUBLIC "${PROJECT_ROOT}" )
target_link_libraries(c_demo PRIVATE xcplib)

# Example struct_demo
set(struct_demo_SOURCES examples/struct_demo/src/main.c ${xcplib_SOURCES} )
add_executable(struct_demo ${struct_demo_SOURCES})
target_include_directories(struct_demo PUBLIC "${PROJECT_ROOT}" )
target_link_libraries(struct_demo PRIVATE xcplib)

# Example multi_thread_demo
set(multi_thread_demo_SOURCES examples/multi_thread_demo/src/main.c ${xcplib_SOURCES} )
add_executable(multi_thread_demo ${multi_thread_demo_SOURCES})
target_include_directories(multi_thread_demo PUBLIC "${PROJECT_ROOT}" )
target_link_libraries(multi_thread_demo PRIVATE xcplib)

if (WINDOWS)

else ()

    target_link_libraries(multi_thread_demo PRIVATE m)

    set_target_properties(c_demo PROPERTIES SUFFIX ".out")
    set_target_properties(hello_xcp PROPERTIES SUFFIX ".out")
    set_target_properties(struct_demo PROPERTIES SUFFIX ".out")
    set_target_properties(multi_thread_demo PROPERTIES SUFFIX ".out")

endif ()
