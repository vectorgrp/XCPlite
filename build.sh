
#!/bin/bash

# ccmake -B build -S .

# Parse command line arguments
BUILD_TYPE="Debug"  # Default to Debug build
COMPILER_CHOICE=""  # Default to system default compiler (no forcing)
CLEAN_BUILD=false   # Whether to clean before building
BUILD_TARGET="examples"  # Default to building library + examples (without bpf_demo)

# Function to show usage
show_usage() {
    echo "Usage: $0 [build_type] [compiler] [target] [options]"
    echo ""
    echo "Parameters:"
    echo "  build_type: debug|release|relwithdebinfo (default: debug)"
    echo "  compiler:   gcc|clang"
    echo "  target:     lib|examples|tests|bpf|all (default: examples)"
    echo ""
    echo "Build Targets:"
    echo "  lib:        Build only the xcplib library"
    echo "  examples:   Build library + examples (excluding bpf_demo) [DEFAULT]"
    echo "  tests:      Build library + test targets (a2l_test, cal_test, type_detection tests)"
    echo "  bpf:        Build library + examples including bpf_demo (Linux only)"
    echo "  all:        Build everything (library + examples + tests + bpf_demo)"
    echo ""
    echo "Options:"
    echo "  clean:      Clean build directory before building"
    echo "  cleanall:   Clean all build directories (build-*, build)"
    echo ""
    echo "Examples:"
    echo "  $0                    # Default: library + examples (no bpf_demo)"
    echo "  $0 lib                # Build only the library"
    echo "  $0 tests              # Build library + test targets"
    echo "  $0 bpf                # Build library + examples including bpf_demo"
    echo "  $0 all                # Build everything"
    echo "  $0 clean              # Clean build and rebuild library + examples"
    echo "  $0 release gcc all    # Release build with GCC, all targets"
    echo "  $0 debug clang tests  # Debug build with Clang, tests only"
    echo "  $0 cleanall           # Clean all build directories and exit"
    echo ""
    echo "Build directories used:"
    echo "  build:                System default compiler builds"
    echo "  build-gcc:            GCC compiler builds"
    echo "  build-clang:          Clang compiler builds"
    echo ""
    echo "Platform-specific targets:"
    echo "  bpf_demo:             Only built on Linux systems (requires BPF support)"
}

# Parse arguments and set correct case for CMake
for arg in "$@"; do
    # Convert to lowercase for comparison
    arg_lower=$(echo "$arg" | tr '[:upper:]' '[:lower:]')
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
        gcc)
            COMPILER_CHOICE="gcc"
            ;;
        clang)
            COMPILER_CHOICE="clang"
            ;;
        default)
            COMPILER_CHOICE="default"
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
        bpf)
            BUILD_TARGET="bpf"
            ;;
        all)
            BUILD_TARGET="all"
            ;;
        clean)
            CLEAN_BUILD=true
            ;;
        cleanall)
            echo "Cleaning all build directories..."
            rm -rf build build-gcc build-clang
            echo "✅ All build directories cleaned"
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

# Build CMake compiler arguments based on choice
CMAKE_COMPILER_ARGS=""
COMPILER_NAME=""
BUILD_DIR="build"  # Default build directory

case "$COMPILER_CHOICE" in
    "gcc")
        CMAKE_COMPILER_ARGS="-DUSE_GCC=ON"
        COMPILER_NAME="GCC"
        BUILD_DIR="build-gcc"
        ;;
    "clang")
        CMAKE_COMPILER_ARGS="-DUSE_CLANG=ON"
        COMPILER_NAME="Clang"
        BUILD_DIR="build-clang"
        ;;
    "default"|"")
        CMAKE_COMPILER_ARGS=""
        COMPILER_NAME="System Default"
        BUILD_DIR="build"
        ;;
esac

echo "Building in $BUILD_TYPE mode with $COMPILER_NAME compiler"
echo "Build directory: $BUILD_DIR"
echo "Build target: $BUILD_TARGET"

# Detect actual system default compiler when using default option
if [ "$COMPILER_CHOICE" = "" ] || [ "$COMPILER_CHOICE" = "default" ]; then
    # Try to detect the actual system compiler
    CC_VERSION=$(cc --version 2>/dev/null | head -n1)
    CXX_VERSION=$(c++ --version 2>/dev/null | head -n1)
    
    if echo "$CC_VERSION" | grep -q "clang"; then
        ACTUAL_COMPILER="Clang"
        if echo "$CC_VERSION" | grep -q "Apple"; then
            ACTUAL_COMPILER="Apple Clang"
        fi
    elif echo "$CC_VERSION" | grep -q "gcc"; then
        ACTUAL_COMPILER="GCC"
    else
        ACTUAL_COMPILER="Unknown"
    fi
    
    echo "System default compiler detected: $ACTUAL_COMPILER"
fi

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ]; then
    echo "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# Gcc has lesser compatibility problems, use gcc for raspberry pi builds, if problems with atomic_uint_least32_t
echo "==================================================================="
echo "Configuring CMake build system..."
echo "==================================================================="
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -S . -B $BUILD_DIR $CMAKE_COMPILER_ARGS

# Be sure EPK is updated before building
touch src/a2l.c

echo ""
echo "==================================================================="
echo "Building targets..."
echo "==================================================================="

# Define all targets to build based on BUILD_TARGET
LIBRARY_TARGET="xcplib"

# Define target groups
EXAMPLE_TARGETS=(
    "hello_xcp" 
    "hello_xcp_cpp"
    "no_a2l_demo"
    "c_demo"
    "cpp_demo"
    "struct_demo"
    "multi_thread_demo"
)

TEST_TARGETS=(
    "a2l_test"
    "cal_test"
    "type_detection_test_c"
    "type_detection_test_cpp"
)

BPF_TARGETS=()
# Add bpf_demo only on Linux systems
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    BPF_TARGETS+=("bpf_demo")
fi

# Determine which targets to build based on BUILD_TARGET
LIBRARY_DEPENDENT_TARGETS=()
INDEPENDENT_TARGETS=()

case "$BUILD_TARGET" in
    "lib")
        echo "Build target: Library only"
        # Only build the library, no other targets
        ;;
    "examples")
        echo "Build target: Library + Examples (excluding bpf_demo)"
        LIBRARY_DEPENDENT_TARGETS=("${EXAMPLE_TARGETS[@]}")
        ;;
    "tests")
        echo "Build target: Library + Tests"
        LIBRARY_DEPENDENT_TARGETS=("${TEST_TARGETS[@]}")
        ;;
    "bpf")
        echo "Build target: Library + Examples (including bpf_demo)"
        LIBRARY_DEPENDENT_TARGETS=("${EXAMPLE_TARGETS[@]}")
        if [[ "$OSTYPE" == "linux-gnu"* ]]; then
            LIBRARY_DEPENDENT_TARGETS+=("${BPF_TARGETS[@]}")
            echo "Linux system detected - bpf_demo will be built with BPF support"
        else
            echo "Non-Linux system detected - bpf_demo will be skipped (BPF only supported on Linux)"
        fi
        ;;
    "all")
        echo "Build target: Everything (Library + Examples + Tests + BPF)"
        LIBRARY_DEPENDENT_TARGETS=("${EXAMPLE_TARGETS[@]}" "${TEST_TARGETS[@]}")
        if [[ "$OSTYPE" == "linux-gnu"* ]]; then
            LIBRARY_DEPENDENT_TARGETS+=("${BPF_TARGETS[@]}")
            echo "Linux system detected - bpf_demo will be built with BPF support"
        else
            echo "Non-Linux system detected - bpf_demo will be skipped (BPF only supported on Linux)"
        fi
        ;;
    *)
        echo "Error: Unknown build target '$BUILD_TARGET'"
        show_usage
        exit 1
        ;;
esac

# Arrays to track success/failure
SUCCESSFUL_TARGETS=()
FAILED_TARGETS=()

echo ""
echo "-------------------------------------------------------------------"
echo "Building core library: $LIBRARY_TARGET"
echo "-------------------------------------------------------------------"

# Build the core library first
if make --directory ./$BUILD_DIR $LIBRARY_TARGET > /dev/null 2>&1; then
    echo "✅ SUCCESS: $LIBRARY_TARGET compiled successfully"
    echo ""
    SUCCESSFUL_TARGETS+=("$LIBRARY_TARGET")
    
    # If BUILD_TARGET is "lib", we're done after building the library
    if [ "$BUILD_TARGET" = "lib" ]; then
        echo "Library-only build completed successfully."
    else
        # Build BPF program if bpf_demo is in the target list
        if [[ " ${LIBRARY_DEPENDENT_TARGETS[@]} " =~ " bpf_demo " ]]; then
            echo ""
            echo "-------------------------------------------------------------------"
            echo "Building BPF program for bpf_demo..."
            echo "-------------------------------------------------------------------"
            
            # Check if build_bpf.sh exists
            if [ -f "examples/bpf_demo/build_bpf.sh" ]; then
                # Make the script executable
                chmod +x examples/bpf_demo/build_bpf.sh
                
                # Run the BPF build script
                if examples/bpf_demo/build_bpf.sh > /dev/null 2>&1; then
                    echo "✅ SUCCESS: BPF program compiled successfully"
                    
                    # Copy BPF object file to the correct build directory
                    BPF_OBJ_SRC="examples/bpf_demo/src/process_monitor.bpf.o"
                    if [ -f "$BPF_OBJ_SRC" ]; then
                        cp "$BPF_OBJ_SRC" "$BUILD_DIR/"
                        echo "   BPF object file copied to $BUILD_DIR/"
                    else
                        echo "⚠️  WARNING: BPF object file not found at $BPF_OBJ_SRC"
                    fi
                else
                    echo "❌ FAILED: BPF program build failed"
                    echo "   BPF build error details:"
                    examples/bpf_demo/build_bpf.sh 2>&1 | sed 's/^/   /'
                fi
            else
                echo "⚠️  WARNING: BPF build script not found at examples/bpf_demo/build_bpf.sh"
            fi
            echo ""
        fi
        
        # Build library-dependent targets only if library succeeded
        for target in "${LIBRARY_DEPENDENT_TARGETS[@]}"; do
            if make --directory ./$BUILD_DIR $target > /dev/null 2>&1; then
                SUCCESSFUL_TARGETS+=("$target")
            else
                echo "❌ FAILED: $target compilation failed"
                echo "   Error details:"
                make --directory ./$BUILD_DIR $target 2>&1 | sed 's/^/   /'
                FAILED_TARGETS+=("$target")
            fi
        done
    fi
    
else
    echo "❌ CRITICAL FAILURE: $LIBRARY_TARGET compilation failed"
    echo "   Error details:"
    make --directory ./$BUILD_DIR $LIBRARY_TARGET 2>&1 | sed 's/^/   /'
    echo ""
    echo "🛑 Library build failed, but proceeding with independent targets..."
    echo "   Library-dependent targets will be skipped."
    echo ""
    FAILED_TARGETS+=("$LIBRARY_TARGET")
    
    # Note: We don't add library-dependent targets to FAILED_TARGETS 
    # since they weren't actually attempted
    if [ ${#LIBRARY_DEPENDENT_TARGETS[@]} -gt 0 ]; then
        echo "📋 SKIPPED TARGETS (due to library failure):"
        for target in "${LIBRARY_DEPENDENT_TARGETS[@]}"; do
            echo "   - $target (skipped - requires xcplib)"
        done
        echo ""
    fi
fi

# Build independent targets regardless of library status (only for certain build targets)
if [ ${#INDEPENDENT_TARGETS[@]} -gt 0 ]; then
    for target in "${INDEPENDENT_TARGETS[@]}"; do
        if make --directory ./$BUILD_DIR $target > /dev/null 2>&1; then
            SUCCESSFUL_TARGETS+=("$target")
        else
            echo "❌ FAILED: $target compilation failed"
            echo "   Error details:"
            make --directory ./$BUILD_DIR $target 2>&1 | sed 's/^/   /'
            FAILED_TARGETS+=("$target")
        fi
    done
fi

echo ""
echo "==================================================================="
echo "BUILD SUMMARY"
echo "==================================================================="
echo "Build Configuration: $BUILD_TYPE mode with $COMPILER_NAME compiler"
if [ "$COMPILER_CHOICE" = "" ] || [ "$COMPILER_CHOICE" = "default" ]; then
    echo "System Compiler: $ACTUAL_COMPILER"
fi
echo "Build Directory: $BUILD_DIR"
echo "Build Target: $BUILD_TARGET"
echo ""

if [ ${#FAILED_TARGETS[@]} -eq 0 ]; then
    echo "✅ All targets compiled successfully!"
    echo ""
else
    echo ""
    echo "✅ SUCCESSFUL TARGETS (${#SUCCESSFUL_TARGETS[@]}):"
    if [ ${#SUCCESSFUL_TARGETS[@]} -eq 0 ]; then
        echo "   None"
    else
        for target in "${SUCCESSFUL_TARGETS[@]}"; do
            echo "   - $target"
        done
    fi

    echo ""
    echo "❌ FAILED TARGETS (${#FAILED_TARGETS[@]}):"
    for target in "${FAILED_TARGETS[@]}"; do
        if [ "$target" = "xcplib" ]; then
            echo "   - $target (CRITICAL - core library failure)"
        else
            echo "   - $target"
        fi
    done

    echo ""
    if [[ " ${FAILED_TARGETS[@]} " =~ " xcplib " ]]; then
        echo "💥 LIBRARY FAILURE: Core library (xcplib) failed to compile."
        echo "   Library-dependent targets were skipped."
    fi
fi

echo ""
echo "==================================================================="

# Exit with error code if any target failed
if [ ${#FAILED_TARGETS[@]} -gt 0 ]; then
    if [ "$COMPILER_CHOICE" = "" ] || [ "$COMPILER_CHOICE" = "default" ]; then
        echo "Total: ${#SUCCESSFUL_TARGETS[@]} successful, ${#FAILED_TARGETS[@]} failed ($BUILD_TYPE build with $COMPILER_NAME - $ACTUAL_COMPILER)"
    else
        echo "Total: ${#SUCCESSFUL_TARGETS[@]} successful, ${#FAILED_TARGETS[@]} failed ($BUILD_TYPE build with $COMPILER_NAME)"
    fi
    echo "==================================================================="
    exit 1
else
    if [ "$COMPILER_CHOICE" = "" ] || [ "$COMPILER_CHOICE" = "default" ]; then
        echo "Build completed successfully: $BUILD_TYPE mode with $COMPILER_NAME compiler ($ACTUAL_COMPILER)"
    else
        echo "Build completed successfully: $BUILD_TYPE mode with $COMPILER_NAME compiler"
    fi
    echo "==================================================================="
    echo ""
    exit 0
fi


