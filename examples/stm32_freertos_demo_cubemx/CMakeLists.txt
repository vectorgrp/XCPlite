cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32H753EthDemo)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    
    # XCPlite demo 
    xcp_demo/xcp_demo.c

    # XCPlite library
    /git/XCPlite-RainerZ/src/xcpappl.c 
    /git/XCPlite-RainerZ/src/xcplite.c 
    /git/XCPlite-RainerZ/src/xcpethserver.c 
    /git/XCPlite-RainerZ/src/xcpethtl.c 
    /git/XCPlite-RainerZ/src/queue32.c 
    /git/XCPlite-RainerZ/src/cal.c 
    /git/XCPlite-RainerZ/src/platform.c

)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    /git/XCPlite-RainerZ/src
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    _FREE_RTOS                                          # Force FreeRTOS code paths in platform.h/platform.c, xcpappl.c
    "XCPLIB_CFG_OVERRIDE=\"xcplib_rtos_cfg.h\""

)






# Workaround for STM32H7 CMSIS header attribute warnings with starm-clang
# Reference: https://community.st.com/t5/stm32cubeide-for-visual-studio/warning-quot-unknown-attribute-optimize-quot-cubeide-vscode-3-8/td-p/884776
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(stm32cubemx INTERFACE
        -Wno-ignored-attributes
        -Wno-unknown-attributes
        -Wno-section
    )
endif()

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
