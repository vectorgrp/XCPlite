
#!/bin/bash

# ccmake -B build -S .

# Parse command line arguments
BUILD_TYPE="Debug"  # Default to Debug build
USE_CLANG="OFF"     # Default to GCC (better compatibility for Raspberry Pi)

# Function to show usage
show_usage() {
    echo "Usage: $0 [build_type] [compiler]"
    echo ""
    echo "Parameters:"
    echo "  build_type: debug|release (default: debug)"
    echo "  compiler:   gcc|clang (default: gcc)"
    echo ""
    echo "Examples:"
    echo "  $0                    # Debug build with GCC (default)"
    echo "  $0 release            # Release build with GCC"
    echo "  $0 debug clang        # Debug build with Clang"
    echo "  $0 release clang      # Release build with Clang"
    echo "  $0 gcc                # Debug build with GCC (compiler as first param)"
    echo "  $0 clang              # Debug build with Clang (compiler as first param)"
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
            USE_CLANG="OFF"
            ;;
        clang)
            USE_CLANG="ON"
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

# Display selected configuration
COMPILER_NAME=""
if [ "$USE_CLANG" = "ON" ]; then
    COMPILER_NAME="Clang"
else
    COMPILER_NAME="GCC"
fi
BUILD_TYPE_UPPER=$(echo "$BUILD_TYPE" | tr '[:lower:]' '[:upper:]')
echo "Building in $BUILD_TYPE_UPPER mode with $COMPILER_NAME compiler"

# Gcc has lesser compatibility problems, use gcc for raspberry pi builds, if problems with atomic_uint_least32_t
echo "==================================================================="
echo "Configuring CMake build system..."
echo "==================================================================="
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -S . -B build -DUSE_CLANG=$USE_CLANG

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
if make --directory ./build $LIBRARY_TARGET > /dev/null 2>&1; then
    echo "✅ SUCCESS: $LIBRARY_TARGET compiled successfully"
    echo ""
    SUCCESSFUL_TARGETS+=("$LIBRARY_TARGET")
    
    # Build library-dependent targets only if library succeeded
    for target in "${LIBRARY_DEPENDENT_TARGETS[@]}"; do
        if make --directory ./build $target > /dev/null 2>&1; then
            SUCCESSFUL_TARGETS+=("$target")
        else
            echo "❌ FAILED: $target compilation failed"
            echo "   Error details:"
            make --directory ./build $target 2>&1 | sed 's/^/   /'
            FAILED_TARGETS+=("$target")
        fi
    done
    
else
    echo "❌ CRITICAL FAILURE: $LIBRARY_TARGET compilation failed"
    echo "   Error details:"
    make --directory ./build $LIBRARY_TARGET 2>&1 | sed 's/^/   /'
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
    if make --directory ./build $target > /dev/null 2>&1; then
        SUCCESSFUL_TARGETS+=("$target")
    else
        echo "❌ FAILED: $target compilation failed"
        echo "   Error details:"
        make --directory ./build $target 2>&1 | sed 's/^/   /'
        FAILED_TARGETS+=("$target")
    fi
done

echo ""
echo "==================================================================="
echo "BUILD SUMMARY"
echo "==================================================================="

if [ ${#FAILED_TARGETS[@]} -eq 0 ]; then
    echo ""
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
    echo "Total: ${#SUCCESSFUL_TARGETS[@]} successful, ${#FAILED_TARGETS[@]} failed"
    echo "==================================================================="
    exit 1
else
    echo "==================================================================="
    exit 0
fi


