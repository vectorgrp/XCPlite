# c_demo

cmake_minimum_required(VERSION 3.5)

project(xcplib VERSION 1.0.0 LANGUAGES C CXX)

# Enable compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable test
if(NOT DEFINED BUILD_TEST)
    set(BUILD_TEST TRUE)
    endif()
if(BUILD_TEST)
message( "Build test enabled")    
else()
endif()
    

# Enable doc
if(NOT DEFINED ENABLE_DOC)
    set(ENABLE_DOC TRUE)
endif()
if(ENABLE_DOC)
message( "Docs enabled")    
endif()

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
set(xcplib_SOURCES src/xcpAppl.c src/xcpLite.c src/xcpEthServer.c src/xcpEthTl.c src/xcpQueue64.c src/a2l.c src/platform.c )   
else()
set(xcplib_SOURCES src/xcpAppl.c src/xcpLite.c src/xcpEthServer.c src/xcpEthTl.c  src/xcpQueue32.c src/a2l.c src/platform.c )   
endif()


set_source_files_properties(${xcplib_SOURCES} PROPERTIES LANGUAGE C)
add_library(xcplib ${xcplib_SOURCES})
target_include_directories(xcplib PUBLIC "${PROJECT_ROOT}/" "${PROJECT_ROOT}/src" )
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
add_executable(hello_xcp examples/hello_xcp/src/main.c)
target_include_directories(hello_xcp PUBLIC "${PROJECT_ROOT}" "${PROJECT_ROOT}/src")
target_link_libraries( hello_xcp PRIVATE xcplib)

# Example c_demo
add_executable(c_demo examples/c_demo/src/main.c)
target_include_directories(c_demo PUBLIC "${PROJECT_ROOT}" "${PROJECT_ROOT}/src")
target_link_libraries(c_demo PRIVATE xcplib)

# Example struct_demo
add_executable(struct_demo examples/struct_demo/src/main.c)
target_include_directories(struct_demo PUBLIC "${PROJECT_ROOT}" "${PROJECT_ROOT}/src")
target_link_libraries(struct_demo PRIVATE xcplib)

# Example multi_thread_demo
add_executable(multi_thread_demo examples/multi_thread_demo/src/main.c)
target_include_directories(multi_thread_demo PUBLIC "${PROJECT_ROOT}" "${PROJECT_ROOT}/src")
target_link_libraries(multi_thread_demo PRIVATE xcplib)

# Example cpp_demo
add_executable(cpp_demo examples/cpp_demo/src/main.cpp examples/cpp_demo/src/sig_gen.cpp)
target_compile_options(cpp_demo PRIVATE -std=c++11 )
target_include_directories(cpp_demo PUBLIC "${PROJECT_ROOT}" "${PROJECT_ROOT}/src")
target_link_libraries(cpp_demo PRIVATE xcplib)


# Tests
if(BUILD_TEST)

# Type detection test (C version)
#set_source_files_properties(test/type_detection_test/src/main.c PROPERTIES LANGUAGE C)
add_executable(type_detection_test_c test/type_detection_test/src/main.c)
target_include_directories(type_detection_test_c PUBLIC "${PROJECT_ROOT}" )
target_link_libraries(type_detection_test_c PRIVATE xcplib)

# Type detection test (C++ version)
#set_source_files_properties(test/type_detection_test/src/main.cpp PROPERTIES LANGUAGE CXX)
add_executable(type_detection_test_cpp test/type_detection_test/src/main.cpp)
target_compile_options(type_detection_test_cpp PRIVATE -std=c++11 )
target_include_directories(type_detection_test_cpp PUBLIC "${PROJECT_ROOT}" )
target_link_libraries(type_detection_test_cpp PRIVATE xcplib)

# A2L generation test (C version)
#set_source_files_properties(test/type_detection_test/src/main.c PROPERTIES LANGUAGE C)
add_executable(a2l_test test/a2l_test/src/main.c)
target_include_directories(a2l_test PUBLIC "${PROJECT_ROOT}" )
target_link_libraries(a2l_test PRIVATE xcplib)


endif()


if (WINDOWS)

else ()

    target_link_libraries(multi_thread_demo PRIVATE m)

    set_target_properties(c_demo PROPERTIES SUFFIX ".out")
    set_target_properties(hello_xcp PROPERTIES SUFFIX ".out")
    set_target_properties(struct_demo PROPERTIES SUFFIX ".out")
    set_target_properties(multi_thread_demo PROPERTIES SUFFIX ".out")
    set_target_properties(cpp_demo PROPERTIES SUFFIX ".out")
    if(BUILD_TEST)
        set_target_properties(type_detection_test_c PROPERTIES SUFFIX ".out")
        set_target_properties(type_detection_test_cpp PROPERTIES SUFFIX ".out")
        set_target_properties(a2l_test PROPERTIES SUFFIX ".out")
    endif()


endif ()



if (ENABLE_DOC)

# Find Doxygen for documentation generation
find_package(Doxygen OPTIONAL_COMPONENTS dot mscgen dia)
if(DOXYGEN_FOUND)

    # Set Doxygen variables
    set(DOXYGEN_INPUT_DIR ${PROJECT_SOURCE_DIR})
    set(DOXYGEN_OUTPUT_DIR ${PROJECT_SOURCE_DIR}/docs)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)
    
    # Create the Doxyfile from template if needed
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    
    # Create output directory
    file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR})
    
    # Custom target for generating documentation
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    # Custom target for cleaning documentation
    add_custom_target(docs-clean
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${DOXYGEN_OUTPUT_DIR}
        COMMENT "Cleaning generated documentation"
    )
    
    # Custom target for opening documentation in browser (macOS/Linux)
    if(APPLE)
        add_custom_target(docs-open
            COMMAND open ${DOXYGEN_INDEX_FILE}
            DEPENDS docs
            COMMENT "Opening documentation in default browser"
        )
    elseif(UNIX)
        add_custom_target(docs-open
            COMMAND xdg-open ${DOXYGEN_INDEX_FILE}
            DEPENDS docs
            COMMENT "Opening documentation in default browser"
        )
    endif()
    
    message(STATUS "Doxygen found: ${DOXYGEN_EXECUTABLE}")
    message(STATUS "Documentation will be generated in: ${DOXYGEN_OUTPUT_DIR}")
    message(STATUS "Use 'make docs' to generate documentation")
    message(STATUS "Use 'make docs-open' to generate and open documentation")
else()
    message(STATUS "Doxygen not found. Documentation generation not available.")
    message(STATUS "Install doxygen to enable documentation generation:")
    if(APPLE)
        message(STATUS "  brew install doxygen")
        message(STATUS "  brew install graphviz  # Optional: for graphs")
    elseif(UNIX)
        message(STATUS "  sudo apt-get install doxygen")
        message(STATUS "  sudo apt-get install graphviz  # Optional: for graphs")
    endif()
endif()
endif()




