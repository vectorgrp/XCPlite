#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parse command line arguments
BUILD_TYPE="Debug"  # Default to Debug build
CLEAN_BUILD=false   # Whether to clean before building
BUILD_TARGET="examples"  # Default to building library + standard examples
INSTALL_LIBRARY=false   # Whether to install library after building
INSTALL_PREFIX=""       # Custom install prefix (empty = use CMake default)
RUN_CLANG_TIDY=false    # Whether to run clang-tidy on library sources
BUILD_FREERTOS_DEMO=false  # Whether to build the FreeRTOS POSIX simulator demo
BUILD_NO_A2L_DEMO=false    # Whether to build no_a2l_demo with XCPLIB_NO_A2L configuration
BUILD_SHM_TOOLS=false          # Whether to build SHM mode tool targets (shmtool,xcpdaemon)
BUILD_PTP_TOOLS=false          # Whether to build PTP tool targets (ptptool)
BUILD_RUST_TOOLS=false     # Whether to build Rust tool targets (xcpclient, bintool) via cargo

# Function to show usage
show_usage() {
    echo "Usage: $0 [build_type] [target] [options]"
    echo ""
    echo "Parameters:"
    echo "  build_type:    debug|release|relwithdebinfo (default: debug)"
    echo "  target:        lib|examples|tests|tools|rust|all (default: examples)"
    echo ""
    echo "Build Targets:"
    echo "  lib:            Build only the xcplite library"
    echo "  examples:       Build library + standard examples [DEFAULT]"
    echo "  tests:          Build library + test targets (a2l_test, cal_test, daq_test, queue_test, clock_test, ...)"
    echo "  shm_tools:      Build SHM mode tool targets (shmtool,xcpdaemon)"
    echo "  ptp_tools:      Build PTP tool targets (ptptool)"
    echo "  rust_tools:     Build Rust tool targets (xcpclient, bintool) via CMake/cargo"
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
    echo "  CC=gcc CXX=g++ $0 release all    # Release build with GCC, all targets"
    echo "  CC=clang CXX=clang++ $0 tests    # Build with Clang, tests only"
    echo "  $0 lib tidy                      # Build library and run clang-tidy"
    echo "  $0 freertos                      # Build freertos_demo (POSIX xcplite internals)"
    echo "  $0 no_a2l_demo                   # Build no_a2l_demo with XCPLIB_NO_A2L (no A2L generator)"
    echo "  $0 shm_tools                     # Build SHM mode tool targets (shmtool,xcpdaemon)"
    echo "  $0 ptp_tools                     # Build PTP tool targets (ptptool)"
    echo "  $0 rust_tools                    # Build Rust tool targets (xcpclient, bintool) via CMake/cargo"
    echo "  $0 rust_tools install             # Build and install Rust tools to build/install/bin"
    echo ""
    echo "Platform-specific targets:"
    echo "  bpf_demo:             Only built on Linux when libbpf is available (automatic detection)"
    echo "  freertos_demo:        FreeRTOS POSIX simulator demo (macOS/Linux only, downloads FreeRTOS-Kernel)"
    echo "  no_a2l_demo:          Build without A2L generator (XCPLIB_NO_A2L), uses dedicated build_no_a2l/ directory"
    echo "  xcpclient, bintool:   Rust tools, built via CMake custom targets (requires cargo)"
    echo "  shmtool, xcpdaemon:   SHM mode tools (macOS/Linux only)"
    echo "  ptptool:              PTP tool (Linux only, requires hardware timestamping support)"
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

    # freertos: build freertos_demo with standard POSIX xcplite internals
    if [[ "$arg_lower" == "freertos" ]]; then
        BUILD_FREERTOS_DEMO=true
        continue
    fi

    # no_a2l_demo: build no_a2l_demo with XCPLIB_NO_A2L (disables A2L generator in xcplite)
    # Uses a dedicated build directory since XCPLIB_NO_A2L breaks other examples.
    if [[ "$arg_lower" == "no_a2l_demo" ]]; then
        BUILD_NO_A2L_DEMO=true
        continue
    fi

    # shm: build SHM mode tool targets (shmtool,xcpdaemon) via cargo
    if [[ "$arg_lower" == "shm_tools" ]]; then
        BUILD_SHM_TOOLS=true
        continue
    fi

    # ptp: build PTP tool targets (ptptool) via cargo
    if [[ "$arg_lower" == "ptp_tools" ]]; then
        BUILD_PTP_TOOLS=true
        continue
    fi  

    # rust: build Rust tool targets (xcpclient, bintool) via cargo
    if [[ "$arg_lower" == "rust_tools" ]]; then
        BUILD_RUST_TOOLS=true
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
        shm_tools)
            BUILD_TARGET="shm_tools"
            ;;
        ptp_tools)
            BUILD_TARGET="ptp_tools"
            ;;
        rust_tools)
            BUILD_TARGET="rust_tools"
            ;;
        clean)
            CLEAN_BUILD=true
            ;;
        cleanall)
            echo "Cleaning all build and test artefacts ..."
            rm -rf build build_no_a2l_demo build_freertos build_ptptool
            echo "Build directories cleaned"
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

# Standard build directory
BUILD_DIR="build"


# Determine CMake options based on BUILD_TARGET
CMAKE_EXAMPLES_FLAG=""
CMAKE_TESTS_FLAG=""
CMAKE_SHM_TOOLS_FLAG=""
CMAKE_PTP_TOOLS_FLAG=""

case "$BUILD_TARGET" in
    "lib")
        # Only build the library, disable examples, tests and tools
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
        CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=OFF"
        CMAKE_PTP_TOOLS_FLAG="-DXCPLITE_BUILD_PTP_TOOLS=OFF"
        ;;
    "examples")
        # Build library + examples (CMake will handle bpf_demo based on platform and libbpf availability)
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=ON"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
        CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=OFF"
        CMAKE_PTP_TOOLS_FLAG="-DXCPLITE_BUILD_PTP_TOOLS=OFF"
        ;;
    "tests")
        # Build library + tests, no examples or tools
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=ON"
        CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=OFF"
        CMAKE_PTP_TOOLS_FLAG="-DXCPLITE_BUILD_PTP_TOOLS=OFF"
        ;;
    "shm_tools")
        # Build library + tools, no examples or tests
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
        CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=ON"
        CMAKE_PTP_TOOLS_FLAG="-DXCPLITE_BUILD_PTP_TOOLS=OFF"
        ;;
    "ptp_tools")
        # Build library + tools, no examples or tests
        CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
        CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
        CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=OFF"
        CMAKE_PTP_TOOLS_FLAG="-DXCPLITE_BUILD_PTP_TOOLS=ON"
        ;;
    *)
        echo "Error: Unknown build target '$BUILD_TARGET'"
        show_usage
        exit 1
        ;;
esac



echo ""
echo "==================================================================="
echo "Configuring CMake build system..."
echo "==================================================================="

echo "Building in $BUILD_TYPE mode"
echo "Build target: $BUILD_TARGET"


# Add custom install prefix if specified
CMAKE_INSTALL_ARGS=""
if [ -n "$INSTALL_PREFIX" ]; then
    CMAKE_INSTALL_ARGS="-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
    echo "Custom install prefix: $INSTALL_PREFIX"
fi
echo "Install args: $CMAKE_INSTALL_ARGS"


CMAKE_RUST_TOOLS_FLAG="-DXCPLITE_BUILD_RUST_TOOLS=OFF"
if [ "$BUILD_RUST_TOOLS" = true ]; then
    CMAKE_RUST_TOOLS_FLAG="-DXCPLITE_BUILD_RUST_TOOLS=ON"
    echo "Rust tools build enabled: xcpclient, bintool (built via CMake custom targets, requires cargo)"
    echo "Flags: -DXCPLITE_BUILD_RUST_TOOLS=ON"
fi

CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=OFF"
if [ "$BUILD_SHM_TOOLS" = true ]; then
    CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=ON"
    echo "SHM tools build enabled"
    echo "Flags: -DXCPLITE_BUILD_SHM_TOOLS=ON"
fi

CMAKE_NO_A2L_FLAGS="-DXCPLITE_BUILD_NO_A2L_DEMO=OFF"
if [ "$BUILD_NO_A2L_DEMO" = true ]; then
    # Rrecompiles xcplite without the A2L generator, which breaks other examples. Use a dedicated build directory.
    BUILD_DIR="build_no_a2l_demo"
    CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
    CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
    CMAKE_PTP_TOOLS_FLAG="-DXCPLITE_BUILD_PTP_TOOLS=OFF"
    CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=OFF"
    CMAKE_RUST_TOOLS_FLAG="-DXCPLITE_BUILD_RUST_TOOLS=OFF"
    CMAKE_NO_A2L_FLAGS="-DXCPLITE_BUILD_NO_A2L_DEMO=ON"
    echo "no_a2l_demo build: Build directory: $BUILD_DIR  (isolated from regular build)"
    echo "Flags: -DXCPLITE_BUILD_NO_A2L_DEMO=ON"
fi

CMAKE_FREERTOS_FLAGS="-DXCPLITE_BUILD_FREERTOS_DEMO=OFF"
if [ "$BUILD_FREERTOS_DEMO" = true ]; then
    # Rrecompiles xcplite without the A2L generator, which breaks other examples. Use a dedicated build directory.
    BUILD_DIR="build_freertos"
    CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
    CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
    CMAKE_PTP_TOOLS_FLAG="-DXCPLITE_BUILD_PTP_TOOLS=OFF"
    CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=OFF"
    CMAKE_RUST_TOOLS_FLAG="-DXCPLITE_BUILD_RUST_TOOLS=OFF"
    CMAKE_FREERTOS_FLAGS="-DXCPLITE_BUILD_FREERTOS_DEMO=ON"
    echo "freertos_demo build: Build directory: $BUILD_DIR  (isolated from regular build)"
    echo "Flags: -DXCPLITE_BUILD_FREERTOS_DEMO=ON"
fi

CMAKE_PTP_TOOLS_FLAGS="-DXCPLITE_BUILD_PTP_TOOLS=OFF"
if [ "$BUILD_PTP_TOOLS" = true ]; then
    # Rrecompiles xcplite with hardware time stamping enabled, which breaks other examples. Use a dedicated build directory.
    BUILD_DIR="build_ptptool"
    CMAKE_EXAMPLES_FLAG="-DXCPLITE_BUILD_EXAMPLES=OFF"
    CMAKE_TESTS_FLAG="-DXCPLITE_BUILD_TESTS=OFF"
    CMAKE_PTP_TOOLS_FLAG="-DXCPLITE_BUILD_PTP_TOOLS=OFF"
    CMAKE_SHM_TOOLS_FLAG="-DXCPLITE_BUILD_SHM_TOOLS=OFF"
    CMAKE_RUST_TOOLS_FLAG="-DXCPLITE_BUILD_RUST_TOOLS=OFF"
    CMAKE_FREERTOS_FLAGS="-DXCPLITE_BUILD_FREERTOS_DEMO=OFF"
    CMAKE_PTP_TOOLS_FLAGS="-DXCPLITE_BUILD_PTP_TOOLS=ON"
    echo "Flags: -DXCPLITE_BUILD_PTP_TOOLS=ON"
fi

echo "Build directory: $BUILD_DIR"
echo ""

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ]; then
    echo "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi


cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_EXAMPLES_FLAG $CMAKE_TESTS_FLAG $CMAKE_PTP_TOOLS_FLAGS $CMAKE_SHM_TOOLS_FLAG $CMAKE_RUST_TOOLS_FLAG $CMAKE_FREERTOS_FLAGS $CMAKE_NO_A2L_FLAGS -S . -B $BUILD_DIR $CMAKE_INSTALL_ARGS

echo ""
echo "==================================================================="
echo "Building targets..."
echo "==================================================================="

echo ""

# Build all targets configured 
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
    echo "Install args: $CMAKE_INSTALL_ARGS"
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
        if [ "$BUILD_RUST_TOOLS" = true ]; then
            echo "  - Tools:    ${CARGO_HOME:-$HOME/.cargo}/bin/  (xcpclient, bintool, via cargo install)"
        fi
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
            "src/util.c"
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
    # On Windows Git Bash, keep the window open so errors are visible.
    if [ "${OS:-}" = "Windows_NT" ] || [ -n "${MSYSTEM:-}" ]; then
        read -r -p "Press Enter to close..."
    fi
    exit 1
fi


