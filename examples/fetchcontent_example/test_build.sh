#!/bin/bash

# Build script for fetchcontent_example
#
#   ./build.sh [gcc|clang] [local] [clean]
#
#   gcc | clang   Build with gcc/g++ or clang/clang++ (default: CC/CXX from the environment,
#                 otherwise the CMake default compiler). Note: on macOS gcc/g++ are clang shims.
#   local         Build against this repository's working tree instead of cloning xcplite
#   clean         Remove the build directory first
#
# Arguments can be given in any order.
# For a compiler which is not in the PATH as 'gcc'/'clang', use the standard CMake
# environment variables instead, e.g.:  CC=gcc-14 CXX=g++-14 ./build.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

usage() {
    sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

CLEAN=false
CMAKE_ARGS=()

for arg in "$@"; do
    case "$arg" in
        gcc)       CC=gcc;   CXX=g++ ;;
        clang)     CC=clang; CXX=clang++ ;;
        local)     CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_XCPLITE=${REPO_ROOT}") ;;
        clean)     CLEAN=true ;;
        -h|--help) usage; exit 0 ;;
        *)         echo "Unknown argument: $arg"; echo ""; usage; exit 1 ;;
    esac
done

# Check the selected compilers exist, cmake's error for a missing compiler is less obvious
for compiler in "${CC:-}" "${CXX:-}"; do
    if [[ -n "$compiler" ]] && ! command -v "$compiler" > /dev/null; then
        echo "Compiler not found in PATH: $compiler"
        exit 1
    fi
done

# Pass the compilers explicitly. CMake caches the compiler on the first configure and
# ignores the CC/CXX environment variables when an existing build directory is reconfigured.
# With -DCMAKE_C/CXX_COMPILER the compiler can be switched without 'clean'; CMake then
# rebuilds all sources with the new compiler.
if [[ -n "${CC:-}" ]];  then CMAKE_ARGS+=("-DCMAKE_C_COMPILER=$CC");    fi
if [[ -n "${CXX:-}" ]]; then CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=$CXX"); fi

if [[ "$CLEAN" == true ]]; then
    echo "Removing ${SCRIPT_DIR}/build"
    rm -rf "${SCRIPT_DIR}/build"
fi

echo "Compiler: ${CC:-default} / ${CXX:-default}"

cd "${SCRIPT_DIR}"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug "${CMAKE_ARGS[@]}"
cmake --build build --parallel
