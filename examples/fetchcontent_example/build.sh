#!/bin/bash

# Build script for fetchcontent_example

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
echo "SCRIPT_DIR=${SCRIPT_DIR}"
echo "REPO_ROOT=${REPO_ROOT}"

cd "${SCRIPT_DIR}"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build 
