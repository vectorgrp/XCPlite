#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Defaults
BUILD_TYPE="Debug"
CONFIGURATION="default"
BUILD_EXAMPLES=false
BUILD_TESTS=false
BUILD_TOOLS=false
BUILD_RUST=false
_ANY_TARGET=false  # set to true when any target keyword is given
CLEAN_BUILD=false
INSTALL_LIBRARY=false
INSTALL_PREFIX=""
CARGO_INSTALL=false
RUN_CLANG_TIDY=false

show_usage() {
    echo "Usage: $0 [build_type] [configuration] [target] [options]"
    echo ""
    echo "Build type (default: debug):"
    echo "  debug | release | relwithdebinfo"
    echo ""
    echo "Configuration (default: default) — selects xcplib_cfg override and build directory:"
    echo "  default   build/           Standard 64-bit, A2L generation, filesystem, UDP/TCP"
    echo "  no_a2l    build-no_a2l/    No on-target A2L; generate A2L from ELF via xcpclient"
    echo "  ptp       build-ptp/       Hardware socket timestamps for PTP (Linux, PTP-capable NIC)"
    echo "  shm       build-shm/       Shared-memory multi-application mode"
    echo "  rtos      build-rtos/      FreeRTOS POSIX simulator (Linux/macOS only)"
    echo "  raw       build-raw/       Raw Ethernet transport, UDP/IPv4 inside xcplib (Linux only)"
    echo ""
    echo "Target (default: examples) — what to build within the configuration:"
    echo "  lib          Library only"
    echo "  examples     Library + examples  [DEFAULT]"
    echo "  tests        Library + tests"
    echo "  tools        Library + tools  (ptp: ptptool | shm: shmtool, xcpdaemon)"
    echo "  rust_tools   Library + Rust tools: xcpclient, bintool  (requires cargo)"
    echo "  all          Library + examples + tests + tools + rust_tools"
    echo ""
    echo "Note: which examples, tests and tools are actually built depends on the"
    echo "      selected configuration — see 'docs/BUILDING.md' for the full matrix."
    echo ""
    echo "Options:"
    echo "  clean              Clean build directory before building"
    echo "  cleanall           Clean all build directories and exit"
    echo "  install            Install xcplite library to <build_dir>/install"
    echo "  install=<path>     Install xcplite library to custom path"
    echo "  cargo_install      Run 'cargo install --locked' for xcpclient and bintool to ~/.cargo/bin"
    echo "                     (only meaningful with rust_tools or all target)"
    echo "  tidy               Run clang-tidy on library sources"
    echo ""
    echo "Note: 'install' and 'cargo_install' are independent:"
    echo "  install         -> cmake --install  (library headers/cmake config to prefix)"
    echo "  cargo_install   -> cargo install    (xcpclient/bintool binaries to ~/.cargo/bin)"
    echo ""
    echo "Compiler selection (standard CMake environment variables):"
    echo "  CC=gcc CXX=g++ $0 ..."
    echo "  CC=clang CXX=clang++ $0 ..."
    echo ""
    echo "Examples:"
    echo "  $0                                  # Library + examples, default config, debug"
    echo "  $0 release                          # Release build, default config, examples"
    echo "  $0 tests                            # Tests, default config, debug"
    echo "  $0 shm tools                        # shm config: shmtool + xcpdaemon"
    echo "  $0 ptp tools                        # ptp config: ptptool (Linux only)"
    echo "  $0 no_a2l examples                  # no_a2l config: no_a2l_demo, no_a2l_demo_cpp"
    echo "  $0 raw examples                     # raw config: udp_raw_demo (Linux only)"
    echo "  $0 rtos examples                    # rtos config: freertos_demo (Linux/macOS only)"
    echo "  $0 lib install                      # Library only, install to build/install"
    echo "  $0 release lib install=/usr/local   # Release build, install to /usr/local"
    echo "  $0 rust_tools cargo_install         # Build + install xcpclient/bintool to ~/.cargo/bin"
    echo "  $0 clean examples                   # Clean rebuild"
    echo "  $0 cleanall                         # Clean all build directories"
    echo "  $0 lib tidy                         # Build library and run clang-tidy"
    echo "  CC=gcc CXX=g++ $0 release all       # Release build with GCC, all targets (requires cargo for rust_tools)"
}

# Argument parsing
for arg in "$@"; do
    arg_lower=$(echo "$arg" | tr '[:upper:]' '[:lower:]')

    # install[=path] handled before the case statement
    if [[ "$arg" == "install" ]]; then
        INSTALL_LIBRARY=true
        continue
    elif [[ "$arg" == install=* ]]; then
        INSTALL_LIBRARY=true
        INSTALL_PREFIX="${arg#install=}"
        continue
    elif [[ "$arg_lower" == "cargo_install" ]]; then
        CARGO_INSTALL=true
        continue
    fi

    case "$arg_lower" in
        # Build type
        debug)          BUILD_TYPE="Debug" ;;
        release)        BUILD_TYPE="Release" ;;
        relwithdebinfo) BUILD_TYPE="RelWithDebInfo" ;;

        # Configuration
        default|no_a2l|ptp|shm|rtos|raw) CONFIGURATION="$arg_lower" ;;

        # Target (multiple targets may be combined, e.g. "tools examples")
        lib)        _ANY_TARGET=true ;;  # library only: all extras remain false
        examples)   BUILD_EXAMPLES=true; _ANY_TARGET=true ;;
        tests)      BUILD_TESTS=true;    _ANY_TARGET=true ;;
        tools)      BUILD_TOOLS=true;    _ANY_TARGET=true ;;
        rust_tools) BUILD_RUST=true;     _ANY_TARGET=true ;;
        all)        BUILD_EXAMPLES=true; BUILD_TESTS=true; BUILD_TOOLS=true; BUILD_RUST=true; _ANY_TARGET=true ;;

        # Options
        tidy)    RUN_CLANG_TIDY=true ;;
        clean)   CLEAN_BUILD=true ;;
        cleanall)
            echo "Cleaning all build directories..."
            rm -rf build build-no_a2l build-ptp build-shm build-rtos build-raw
            # Also clean legacy directory names from older builds
            rm -rf build_no_a2l_demo build_freertos build_ptptool build_shm
            rm -f ./*.bin ./*.hex ./*.log ./*.mf4 ./*.a2l
            echo "Done"
            exit 0
            ;;
        -h|--help|help)
            show_usage
            exit 0
            ;;
        *)
            echo "Error: Unknown argument '$arg'"
            echo ""
            show_usage
            exit 1
            ;;
    esac
done

# Build directory follows configuration
if [[ "$CONFIGURATION" == "default" ]]; then
    BUILD_DIR="build"
else
    BUILD_DIR="build-${CONFIGURATION}"
fi

# Default to examples if no target was specified
if [[ "$_ANY_TARGET" == false ]]; then
    BUILD_EXAMPLES=true
fi

# Translate to CMake flags
CMAKE_BUILD_EXAMPLES=$([[ "$BUILD_EXAMPLES" == true ]] && echo "ON" || echo "OFF")
CMAKE_BUILD_TESTS=$([[ "$BUILD_TESTS" == true ]] && echo "ON" || echo "OFF")
CMAKE_BUILD_TOOLS=$([[ "$BUILD_TOOLS" == true ]] && echo "ON" || echo "OFF")
CMAKE_BUILD_RUST=$([[ "$BUILD_RUST" == true ]] && echo "ON" || echo "OFF")

# Optional install prefix
CMAKE_INSTALL_ARGS=""
if [[ -n "$INSTALL_PREFIX" ]]; then
    CMAKE_INSTALL_ARGS="-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
fi

# Build display target list
_TARGET_LIST=()
[[ "$CMAKE_BUILD_EXAMPLES" == "ON" ]] && _TARGET_LIST+=("examples")
[[ "$CMAKE_BUILD_TESTS" == "ON" ]]    && _TARGET_LIST+=("tests")
[[ "$CMAKE_BUILD_TOOLS" == "ON" ]]    && _TARGET_LIST+=("tools")
[[ "$CMAKE_BUILD_RUST" == "ON" ]]     && _TARGET_LIST+=("rust_tools")
[[ ${#_TARGET_LIST[@]} -eq 0 ]]       && _TARGET_LIST+=("lib")
_COMPILER_DISPLAY="${CC:-default}${CXX:+ / $CXX}"
echo "Configuration : $CONFIGURATION  ($BUILD_DIR)"
echo "Build type    : $BUILD_TYPE"
echo "Target        : ${_TARGET_LIST[*]}"
echo "Compiler      : $_COMPILER_DISPLAY"
echo ""

# Clean if requested
if [[ "$CLEAN_BUILD" == true ]]; then
    echo "Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
    echo ""
fi

# Explicit compiler flags: pass -DCMAKE_C/CXX_COMPILER when CC/CXX are set in the environment.
# CMake caches the compiler after the first configure and silently ignores CC/CXX on
# re-configures of an existing build directory.  Passing the flag explicitly overrides
# the cache so compiler switching works even without 'clean'.
CMAKE_COMPILER_ARGS=()
if [[ -n "${CC:-}" ]];  then CMAKE_COMPILER_ARGS+=("-DCMAKE_C_COMPILER=$CC");   fi
if [[ -n "${CXX:-}" ]]; then CMAKE_COMPILER_ARGS+=("-DCMAKE_CXX_COMPILER=$CXX"); fi

# Configure
cmake \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DXCPLITE_CONFIGURATION="$CONFIGURATION" \
    -DXCPLITE_BUILD_EXAMPLES="$CMAKE_BUILD_EXAMPLES" \
    -DXCPLITE_BUILD_TESTS="$CMAKE_BUILD_TESTS" \
    -DXCPLITE_BUILD_TOOLS="$CMAKE_BUILD_TOOLS" \
    -DXCPLITE_BUILD_RUST_TOOLS="$CMAKE_BUILD_RUST" \
    "${CMAKE_COMPILER_ARGS[@]}" \
    $CMAKE_INSTALL_ARGS \
    -S . -B "$BUILD_DIR"

echo ""
echo "Building..."

BUILD_SUCCESS=true
if ! cmake --build "$BUILD_DIR" --parallel; then
    echo "Build encountered errors"
    BUILD_SUCCESS=false
fi

# Install library (cmake --install — library headers/cmake config only, never Rust tools)
if [[ "$INSTALL_LIBRARY" == true && "$BUILD_SUCCESS" == true ]]; then
    echo ""
    echo "Installing xcplite library..."
    if cmake --install "$BUILD_DIR" > /dev/null 2>&1; then
        ACTUAL_INSTALL_DIR="${INSTALL_PREFIX:-$BUILD_DIR/install}"
        echo "Installed to: $ACTUAL_INSTALL_DIR"
        echo "  Headers : $ACTUAL_INSTALL_DIR/include/"
        echo "  Library : $ACTUAL_INSTALL_DIR/lib/"
        echo "  CMake   : $ACTUAL_INSTALL_DIR/lib/cmake/xcplite/"
    else
        echo "Installation failed:"
        cmake --install "$BUILD_DIR" 2>&1 | sed 's/^/  /'
        BUILD_SUCCESS=false
    fi
fi

# cargo install — explicit opt-in only; installs xcpclient/bintool to ~/.cargo/bin
if [[ "$CARGO_INSTALL" == true && "$BUILD_SUCCESS" == true ]]; then
    if [[ "$CMAKE_BUILD_RUST" != "ON" ]]; then
        echo "Warning: cargo_install requested but rust_tools target was not built — skipping"
    else
        CARGO_BIN_DIR="${CARGO_HOME:-$HOME/.cargo}/bin"
        echo ""
        echo "Running cargo install for xcpclient and bintool..."
        echo "  Install destination: $CARGO_BIN_DIR"
        CARGO_INSTALL_FLAG=""
        [[ "$BUILD_TYPE" == "Release" || "$BUILD_TYPE" == "RelWithDebInfo" ]] && CARGO_INSTALL_FLAG="" || true
        if cargo install --path "$SCRIPT_DIR/tools/xcpclient" --locked ${CARGO_INSTALL_FLAG:+$CARGO_INSTALL_FLAG} --force; then
            echo "  xcpclient installed to $CARGO_BIN_DIR/xcpclient"
        else
            echo "  xcpclient cargo install failed"
            BUILD_SUCCESS=false
        fi
        if cargo install --path "$SCRIPT_DIR/tools/bintool" --locked ${CARGO_INSTALL_FLAG:+$CARGO_INSTALL_FLAG} --force; then
            echo "  bintool installed to $CARGO_BIN_DIR/bintool"
        else
            echo "  bintool cargo install failed"
            BUILD_SUCCESS=false
        fi
    fi
fi

# clang-tidy
if [[ "$RUN_CLANG_TIDY" == true && "$BUILD_SUCCESS" == true ]]; then
    echo ""
    echo "Running clang-tidy..."
    CLANG_TIDY="clang-tidy"
    if ! command -v "$CLANG_TIDY" &> /dev/null; then
        echo "Error: clang-tidy not found (macOS: brew install llvm)"
        BUILD_SUCCESS=false
    else
        if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
            cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B "$BUILD_DIR"
        fi
        # Keep in sync with xcplite_SOURCES in CMakeLists.txt
        XCPLITE_SOURCES=(
            src/xcpappl.c src/xcplite.c src/xcpethserver.c src/xcpethtl.c
            src/queue32.c src/queue32m.c src/queue64v.c src/queue64f.c src/shm.c
            src/cal.c src/a2l.c src/a2l_writer.c src/persistence.c src/platform.c
            src/sockets.c src/socket_raw.c src/socket_raw_hal_linux.c src/util.c
        )
        for src in "${XCPLITE_SOURCES[@]}"; do
            [[ -f "$SCRIPT_DIR/$src" ]] || { echo "Warning: not found: $src"; continue; }
            echo "  $src"
            "$CLANG_TIDY" -p="$BUILD_DIR" "$SCRIPT_DIR/$src" 2>&1 \
                | grep -v "warnings generated\|Suppressed.*warnings\|Use -header-filter"
        done
        echo "clang-tidy complete"
    fi
fi

echo ""
echo "=================================================================="
echo "Build summary"
echo "=================================================================="
echo "  Configuration : $CONFIGURATION"
echo "  Build type    : $BUILD_TYPE"
echo "  Compiler      : $_COMPILER_DISPLAY"
echo "  Build dir     : $BUILD_DIR"
echo ""

# Print what was actually targeted per configuration
case "$CONFIGURATION" in
    default)
        [[ "$CMAKE_BUILD_EXAMPLES" == "ON" ]] && echo "  Examples      : hello_xcp, hello_xcp_cpp, c_demo, cpp_demo, point_cloud_demo, struct_demo, multi_thread_demo, ptp4l_demo (Linux), bpf_demo (Linux, if XCPLITE_BUILD_BPF_DEMO=ON)" \
            || echo "  Examples      : (not built)"
        [[ "$CMAKE_BUILD_TESTS" == "ON" ]]    && echo "  Tests         : a2l_test, cal_test, daq_test, daq_config_test, clock_test, queue_test, xcp_test, type_detection_test_*" \
            || echo "  Tests         : (not built)"
        [[ "$CMAKE_BUILD_TOOLS" == "ON" ]]    && echo "  Tools         : (none for default configuration)" \
            || echo "  Tools         : (not built)"
        ;;
    no_a2l)
        [[ "$CMAKE_BUILD_EXAMPLES" == "ON" ]] && echo "  Examples      : no_a2l_demo, no_a2l_demo_cpp" \
            || echo "  Examples      : (not built)"
        echo "  Tests         : (none for no_a2l configuration)"
        echo "  Tools         : (none for no_a2l configuration)"
        ;;
    ptp)
        [[ "$CMAKE_BUILD_EXAMPLES" == "ON" ]] && echo "  Examples      : ptp4l_demo (Linux only)" \
            || echo "  Examples      : (not built)"
        [[ "$CMAKE_BUILD_TESTS" == "ON" ]]    && echo "  Tests         : clock_test" \
            || echo "  Tests         : (not built)"
        [[ "$CMAKE_BUILD_TOOLS" == "ON" ]]    && echo "  Tools         : ptptool (Linux only)" \
            || echo "  Tools         : (not built)"
        ;;
    shm)
        [[ "$CMAKE_BUILD_EXAMPLES" == "ON" ]] && echo "  Examples      : hello_xcp (SHM), hello_xcp_cpp (SHM)" \
            || echo "  Examples      : (not built)"
        echo "  Tests         : (none for shm configuration)"
        [[ "$CMAKE_BUILD_TOOLS" == "ON" ]]    && echo "  Tools         : shmtool, xcpdaemon" \
            || echo "  Tools         : (not built)"
        ;;
    rtos)
        [[ "$CMAKE_BUILD_EXAMPLES" == "ON" ]] && echo "  Examples      : freertos_emu_demo (Linux/macOS only; downloads FreeRTOS-Kernel)" \
            || echo "  Examples      : (not built)"
        echo "  Tests         : (none for rtos configuration)"
        echo "  Tools         : (none for rtos configuration)"
        ;;
    raw)
        [[ "$CMAKE_BUILD_EXAMPLES" == "ON" ]] && echo "  Examples      : udp_raw_demo (Linux only, needs CAP_NET_RAW)" \
            || echo "  Examples      : (not built)"
        [[ "$CMAKE_BUILD_TESTS" == "ON" ]]    && echo "  Tests         : socket_raw_test" \
            || echo "  Tests         : (not built)"
        echo "  Tools         : (none for raw configuration)"
        ;;
esac

[[ "$CMAKE_BUILD_RUST" == "ON" ]] && echo "  Rust tools    : xcpclient, bintool" \
    || echo "  Rust tools    : (not built)"

if [[ "$CMAKE_BUILD_RUST" == "ON" ]]; then
    CARGO_SUBDIR="debug"
    [[ "$BUILD_TYPE" == "Release" || "$BUILD_TYPE" == "RelWithDebInfo" ]] && CARGO_SUBDIR="release"
    echo "  Rust binaries : tools/xcpclient/target/$CARGO_SUBDIR/xcpclient"
    echo "                  tools/bintool/target/$CARGO_SUBDIR/bintool"
    if [[ "$CARGO_INSTALL" == true ]]; then
        echo "  cargo install : ${CARGO_HOME:-$HOME/.cargo}/bin/"
    else
        echo "  cargo install : (not run — use cargo_install option to install to ~/.cargo/bin)"
    fi
fi

if [[ "$INSTALL_LIBRARY" == true ]]; then
    echo "  Installed to  : ${INSTALL_PREFIX:-$BUILD_DIR/install}"
fi

echo "=================================================================="
echo ""

if [[ "$BUILD_SUCCESS" == true ]]; then
    exit 0
else
    echo "Build failed — see errors above"
    [[ "${OS:-}" == "Windows_NT" || -n "${MSYSTEM:-}" ]] && read -r -p "Press Enter to close..."
    exit 1
fi