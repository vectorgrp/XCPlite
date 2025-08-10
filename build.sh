
#!/bin/bash

# ccmake -B build -S .

# Parse command line arguments
BUILD_TYPE="Debug"  # Default to Debug build
COMPILER_CHOICE=""  # Default to system default compiler (no forcing)
CLEAN_BUILD=false   # Whether to clean before building

# Function to show usage
show_usage() {
    echo "Usage: $0 [build_type] [compiler] [options]"
    echo ""
    echo "Parameters:"
    echo "  build_type: debug|release (default: debug)"
    echo "  compiler:   gcc|clang"
    echo ""
    echo "Options:"
    echo "  clean:      Clean build directory before building"
    echo "  cleanall:   Clean all build directories (build-*, build)"
    echo ""
    echo "Examples:"
    echo "  $0                    # Debug build with system default compiler"
    echo "  $0 clean              # Clean build directory and debug rebuild with system default compiler"
    echo "  $0 release            # Release build with system default compiler"
    echo "  $0 debug clang        # Debug build with Clang"
    echo "  $0 release gcc        # Release build with GCC"
    echo "  $0 clang clean        # Clean Clang build directory and rebuild"
    echo "  $0 cleanall           # Clean all build directories and exit"
    echo ""
    echo "Build directories used:"
    echo "  build:                System default compiler builds"
    echo "  build-gcc:            GCC compiler builds"
    echo "  build-clang:          Clang compiler builds"
}

# Parse arguments
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
        gcc)
            COMPILER_CHOICE="gcc"
            ;;
        clang)
            COMPILER_CHOICE="clang"
            ;;
        default)
            COMPILER_CHOICE="default"
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

BUILD_TYPE_UPPER=$(echo "$BUILD_TYPE" | tr '[:lower:]' '[:upper:]')
echo "Building in $BUILD_TYPE_UPPER mode with $COMPILER_NAME compiler"
echo "Build directory: $BUILD_DIR"

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

# Define all targets to build
LIBRARY_TARGET="xcplib"
LIBRARY_DEPENDENT_TARGETS=(
    "hello_xcp" 
    "c_demo"
    "struct_demo"
    "multi_thread_demo"
    "cpp_demo"
    "a2l_test"
)
INDEPENDENT_TARGETS=(
    "type_detection_test_c"
    "type_detection_test_cpp"
)

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
    echo "📋 SKIPPED TARGETS (due to library failure):"
    for target in "${LIBRARY_DEPENDENT_TARGETS[@]}"; do
        echo "   - $target (skipped - requires xcplib)"
    done
    echo ""
fi

# Build independent targets regardless of library status
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

echo ""
echo "==================================================================="
echo "BUILD SUMMARY"
echo "==================================================================="
echo "Build Configuration: $BUILD_TYPE_UPPER mode with $COMPILER_NAME compiler"
if [ "$COMPILER_CHOICE" = "" ] || [ "$COMPILER_CHOICE" = "default" ]; then
    echo "System Compiler: $ACTUAL_COMPILER"
fi
echo "Build Directory: $BUILD_DIR"
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
        echo "Total: ${#SUCCESSFUL_TARGETS[@]} successful, ${#FAILED_TARGETS[@]} failed ($BUILD_TYPE_UPPER build with $COMPILER_NAME - $ACTUAL_COMPILER)"
    else
        echo "Total: ${#SUCCESSFUL_TARGETS[@]} successful, ${#FAILED_TARGETS[@]} failed ($BUILD_TYPE_UPPER build with $COMPILER_NAME)"
    fi
    echo "==================================================================="
    exit 1
else
    if [ "$COMPILER_CHOICE" = "" ] || [ "$COMPILER_CHOICE" = "default" ]; then
        echo "Build completed successfully: $BUILD_TYPE_UPPER mode with $COMPILER_NAME compiler ($ACTUAL_COMPILER)"
    else
        echo "Build completed successfully: $BUILD_TYPE_UPPER mode with $COMPILER_NAME compiler"
    fi
    echo "==================================================================="
    exit 0
fi


