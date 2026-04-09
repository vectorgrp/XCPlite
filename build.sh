#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parse command line arguments
BUILD_TYPE="Debug"  # Default to Debug build
CLEAN_BUILD=false   # Whether to clean before building
BUILD_TARGET="examples"  # Default to building library + examples (without bpf_demo)
INSTALL_LIBRARY=false   # Whether to install library after building
INSTALL_PREFIX=""       # Custom install prefix (empty = use CMake default)
RUN_CLANG_TIDY=false    # Whether to run clang-tidy on library sources
QUEUE_TEST_MC_QUEUE=false # Whether to build queue_test with the MC reference queue (xcplite wrapper)
QUEUE_TEST_MC_SHM=false   # Whether to build queue_test for two-process SHM test (MC reference, no wrapper)

# Function to show usage
show_usage() {
    echo "Usage: $0 [build_type] [target] [options]"
    echo ""
    echo "Parameters:"
    echo "  build_type:    debug|release|relwithdebinfo (default: debug)"
    echo "  target:        lib|examples|tests|all (default: examples)"
    echo ""
    echo "Build Targets:"
    echo "  lib:            Build only the xcplite library"
    echo "  examples:       Build library + examples (bpf_demo on Linux if libbpf available) [DEFAULT]"
    echo "  tests:          Build library + test targets (a2l_test, cal_test, daq_test, queue_test, ...)"
    echo "  all:            Build everything (library + examples + tests)"
    echo ""
    echo "Options:"
    echo "  clean:          Clean build directory before building"
    echo "  cleanall:       Clean all build directories and artifacts"
    echo "  install:        Install library to staging directory after building (default location: build/install)"
    echo "  install=<path>: Install library to custom path (e.g., install=/usr/local)"
    echo "  tidy:           Run clang-tidy on library sources after building"
    echo ""
    echo "Compiler Selection:"
    echo "  Use standard CMake environment variables to select compiler:"
    echo "    CC=gcc CXX=g++ $0 [options]        # Use GCC"
    echo "    CC=clang CXX=clang++ $0 [options]  # Use Clang"
    echo ""
    echo "Examples:"
    echo "  $0                               # Default: Build library + examples"
    echo "  $0 clean                         # Clean build and rebuild library + examples"
    echo "  $0 cleanall                      # Clean all artifacts and exit"
    echo "  $0 lib                           # Build only the library"
    echo "  $0 lib install                   # Build library and install to staging directory"
    echo "  $0 release install=/usr/local    # Release build and install to /usr/local"
    echo "  $0 tests                         # Build library + test targets"
    echo "  $0 all                           # Build everything"
    echo "  CC=gcc CXX=g++ $0 release all    # Release build with GCC, all targets"
    echo "  CC=clang CXX=clang++ $0 tests    # Build with Clang, tests only"
    echo "  $0 lib tidy                      # Build library and run clang-tidy"
    echo ""
    echo "Platform-specific targets:"
    echo "  bpf_demo:             Only built on Linux when libbpf is available (automatic detection)"
    echo ""
    echo "Installation:"
    echo "  By default, CMAKE_INSTALL_PREFIX is set to build/install (local staging)."
    echo "  Use 'install' to install to this default location after building."
    echo "  Use 'install=<path>' to override the install prefix and install to a custom location."
    echo "  The installed library can be used by external projects via CMAKE_PREFIX_PATH."
}

# Parse arguments and set correct case for CMake
for arg in "$@"; do
    # Convert to lowercase for comparison
    arg_lower=$(echo "$arg" | tr '[:upper:]' '[:lower:]')
    
    # Check if argument is install with optional path
    if [[ "$arg" == "install" ]]; then
        INSTALL_LIBRARY=true
        continue
    elif [[ "$arg" == install=* ]]; then
        INSTALL_LIBRARY=true
        INSTALL_PREFIX="${arg#install=}"
        continue
    fi
    
    # Check if argument is tidy
    if [[ "$arg" == "tidy" ]]; then
        RUN_CLANG_TIDY=true
        continue
    fi

    # Check if argument is mc_queue or mc_shm
    # This configures the queue_test target 
    # Build with the MC reference queue implementation instead of the XCPlite wrapper
    
    if [[ "$arg_lower" == "mc_queue" ]]; then
        QUEUE_TEST_MC_QUEUE=true
        continue
    fi
    # Enable test the queue in a two-process SHM configuration that is closer to the real use case
    # This is currently only supported on Linux since it relies on robust mutexes for synchronization, but can be extended to other platforms if needed.
    if [[ "$arg_lower" == "mc_shm" ]]; then
        QUEUE_TEST_MC_SHM=true
        continue
    fi
    
    case "$arg_lower" in
        debug)
            BUILD_TYPE="Debug"
            ;;
        release)
            BUILD_TYPE="Release"
            ;;
        relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"
            ;;
        lib|library)
            BUILD_TARGET="lib"
            ;;
        examples)
            BUILD_TARGET="examples"
            ;;
        tests)
            BUILD_TARGET="tests"
            ;;
        all)
            BUILD_TARGET="all"
            ;;
        clean)
            CLEAN_BUILD=true
            ;;
        cleanall)
            echo "Cleaning all build and test artefacts ..."
            rm -rf build
            echo "Build directory cleaned"
            rm -f *.bin
            rm -f *.hex
            rm -f *.log
            rm -f *.mf4
            rm -f *.a2l
            echo "Artifact files cleaned"
            exit 0
            ;;
        -h|--help|help)
            show_usage
            exit 0
            ;;
        *)
            echo "Error: Unknown parameter '$arg'"
            echo ""
            show_usage
            exit 1
            ;;
    esac
done

# Use standard build directory
BUILD_DIR="build"

echo "Building in $BUILD_TYPE mode"
echo "Build directory: $BUILD_DIR"
echo "Build target: $BUILD_TARGET"

# Determine CMake options based on BUILD_TARGET
CMAKE_EXAMPLES_FLAG=""
CMAKE_TESTS_FLAG=""

case "$BUILD_TARGET" in
    "lib")
        # Only build the library, disable examples and tests
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
        ;;
    "examples")
        # Build library + examples (CMake will handle bpf_demo based on platform and libbpf availability)
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=ON"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
        ;;
    "tests")
        # Build library + tests, no examples
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=ON"
        ;;
    "all")
        # Build everything: library + examples + tests
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=ON"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=ON"
        ;;
    *)
        echo "Error: Unknown build target '$BUILD_TARGET'"
        show_usage
        exit 1
        ;;
esac

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ]; then
    echo "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

echo ""
echo "==================================================================="
echo "Configuring CMake build system..."
echo "==================================================================="

# Add custom install prefix if specified
CMAKE_INSTALL_ARGS=""
if [ -n "$INSTALL_PREFIX" ]; then
    CMAKE_INSTALL_ARGS="-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
    echo "Custom install prefix: $INSTALL_PREFIX"
fi

CMAKE_MC_QUEUE_FLAG="-DQUEUE_TEST_MC_QUEUE=OFF -DQUEUE_TEST_MC_SHM=OFF"
[ "$QUEUE_TEST_MC_QUEUE" = true ] && CMAKE_MC_QUEUE_FLAG="-DQUEUE_TEST_MC_QUEUE=ON  -DQUEUE_TEST_MC_SHM=OFF"
[ "$QUEUE_TEST_MC_SHM"   = true ] && CMAKE_MC_QUEUE_FLAG="-DQUEUE_TEST_MC_QUEUE=OFF -DQUEUE_TEST_MC_SHM=ON"

cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_EXAMPLES_FLAG $CMAKE_TESTS_FLAG $CMAKE_MC_QUEUE_FLAG -S . -B $BUILD_DIR $CMAKE_INSTALL_ARGS

echo ""
echo "==================================================================="
echo "Building targets..."
echo "==================================================================="

echo ""

# Build all targets configured by CMake
BUILD_SUCCESS=true
if cmake --build $BUILD_DIR; then
    BUILD_SUCCESS=true
else
    echo "Build encountered errors"
    BUILD_SUCCESS=false
fi

# Install library if requested and build succeeded
if [ "$INSTALL_LIBRARY" = true ] && [ "$BUILD_SUCCESS" = true ]; then
    echo ""
    echo "Installing xcplite library..."
    
    if cmake --install $BUILD_DIR > /dev/null 2>&1; then
        # Determine the actual install directory
        if [ -n "$INSTALL_PREFIX" ]; then
            ACTUAL_INSTALL_DIR="$INSTALL_PREFIX"
        else
            ACTUAL_INSTALL_DIR="$BUILD_DIR/install"
        fi
        
        echo "Library installed to $ACTUAL_INSTALL_DIR"
        echo "  - Library:  $ACTUAL_INSTALL_DIR/lib/"
        echo "  - Headers:  $ACTUAL_INSTALL_DIR/include/"
        echo "  - CMake:    $ACTUAL_INSTALL_DIR/lib/cmake/xcplite/"
    else
        echo "Library installation failed"
        cmake --install $BUILD_DIR 2>&1 | sed 's/^/  /'
        BUILD_SUCCESS=false
    fi
fi

# Run clang-tidy if requested and build succeeded
if [ "$RUN_CLANG_TIDY" = true ] && [ "$BUILD_SUCCESS" = true ]; then
    echo ""
    echo "==================================================================="
    echo "Running clang-tidy on library sources..."
    
    # Check if clang-tidy is available
    CLANG_TIDY="clang-tidy"
    if ! command -v ${CLANG_TIDY} &> /dev/null; then
        echo "Error: clang-tidy not found. Please install it first."
        echo "On macOS: brew install llvm"
        BUILD_SUCCESS=false
    else
        # Ensure compile_commands.json exists
        if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
            echo "compile_commands.json not found. Regenerating..."
            cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B ${BUILD_DIR}
        fi
        
        # XCPlite library source files for clang tidy analysis
        XCPLITE_SOURCES=(
            "src/xcpappl.c"
            "src/xcplite.c"
            "src/xcpethserver.c"
            "src/xcpethtl.c"
            "src/queue32.c"
            "src/queue64v.c"
            "src/queue64f.c"
            "src/shm.c" 
            "src/cal.c" 
            "src/a2l.c"
            "src/persistence.c"
            "src/platform.c"
        )
        
        echo "Build directory: ${BUILD_DIR}"
        
        TOTAL_FILES=0
        
        for src in "${XCPLITE_SOURCES[@]}"; do
            if [ ! -f "${SCRIPT_DIR}/${src}" ]; then
                echo "Warning: Source file not found: ${src}"
                continue
            fi
            
            TOTAL_FILES=$((TOTAL_FILES + 1))
            echo "Analyzing: ${src}"
            
            # Run clang-tidy and filter out the suppressed warnings message
            ${CLANG_TIDY} -p=${BUILD_DIR} ${SCRIPT_DIR}/${src} 2>&1 | grep -v "warnings generated\|Suppressed.*warnings\|Use -header-filter"
        done
        
        echo ""
        echo "Analysis Complete"
        echo "Files analyzed: ${TOTAL_FILES}"
    fi
fi

echo ""
echo "==================================================================="
echo "Build Configuration: $BUILD_TYPE mode"
echo "Build Target: $BUILD_TARGET"
if [ "$INSTALL_LIBRARY" = true ]; then
    if [ -n "$INSTALL_PREFIX" ]; then
        echo "Install Location: $INSTALL_PREFIX"
    else
        echo "Install Location: $BUILD_DIR/install (local staging)"
    fi
fi
echo "==================================================================="
echo ""

if [ "$BUILD_SUCCESS" = true ]; then
    exit 0
else
    echo "Build failed - see error messages above"
    exit 1
fi


