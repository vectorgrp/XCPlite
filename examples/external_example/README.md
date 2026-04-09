# External Example - Using xcplite as a Pre-built Library

This example demonstrates how to use **xcplite as an external library** when you don't want to build it from source as part of your project. This is the typical workflow for:

- **Production deployments** where xcplite is pre-installed
- **System-wide installations** on Linux (e.g., `/usr/local`)
- **Package-based distributions** (e.g., `.deb`, `.rpm`)
- **Development without modifying xcplite** source code

## Key Features

**Independent CMakeLists.txt** - Self-contained project configuration  
**CMake find_package()** - Standard way to locate installed libraries  
**No system installation required** - Uses local staging directory for development  
**Cross-platform** - Works on Linux, macOS, and Windows  

## Project Structure

```
external_example/
├── CMakeLists.txt          # Independent build configuration
├── build.sh                # Automated build script
├── src/
│   └── main.c             # Simple XCP example application for C
│   └── main.cpp           # Simple XCP example application for C++
├── CANape/                # CANape project files (optional)
└── README.md              # This file
```

## Quick Start

The easiest way to build and run this example:

```bash
cd examples/external_example
./build.sh
```

This script will:
1. Build xcplite in the main repository and install it to a local staging directory (`../../build/install`)
2. Configure and build this external example against the installed library

## Manual Build Steps

If you prefer to understand each step:

### 1. Build and Install xcplite

From the repository root:

```bash
# Configure xcplite (installs to build/install by default)
./build.sh release lib install
```

This installs xcplite to `build/install/` with the following structure:
```
build/install/
├── lib/
│   ├── libxcplite.a                    # Static library
│   └── cmake/xcplite/                  # CMake package files
│       ├── xcpliteConfig.cmake
│       ├── xcpliteConfigVersion.cmake
│       └── xcpliteTargets.cmake
└── include/xcplite/                    # Public headers
    ├── xcplite.h
    ├── xcplite.hpp
    ├── a2l.h
    └── a2l.hpp
```

### 2. Build the External Example

From this directory:

```bash
# Configure, pointing to the installed xcplite
cmake -B build -S . -DCMAKE_PREFIX_PATH=../../build/install

# Build
cmake --build build
```

### 3. Run the Example

```bash
./build/external_example
```

Connect with CANape:
- Protocol: XCP on Ethernet
- Address: localhost
- Port: 5555
- Transport: TCP

## How It Works

### CMakeLists.txt

The key parts of this independent CMakeLists.txt:

```cmake
# Find the installed xcplite package
find_package(xcplite REQUIRED)

# Create executable
add_executable(external_example src/main.c)

# Link against xcplite - this automatically provides:
# - Include directories
# - Library dependencies (Threads, math, atomic)
# - Compile definitions
target_link_libraries(external_example PRIVATE xcplite::xcplite)
```

### Finding xcplite

CMake can find xcplite in three ways (in order of precedence):

1. **Via xcplite_DIR** pointing to the cmake config directory:
   ```bash
   cmake -Dxcplite_DIR=../../build/install/lib/cmake/xcplite -B build
   ```

2. **Via CMAKE_PREFIX_PATH** pointing to the install root (recommended):
   ```bash
   cmake -DCMAKE_PREFIX_PATH=../../build/install -B build
   ```

3. **System-wide installation** (if installed to `/usr/local` or similar):
   ```bash
   cmake -B build
   ```

## Production Deployment on Linux

For production use on Linux, install xcplite system-wide:

```bash
# Build and install to /usr/local (requires sudo)
cd /path/to/xcplite-source
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build
```

Then your external project can find it automatically:

```bash
cd /path/to/your-project
cmake -B build -S .
cmake --build build
```

## Linux Package Installation

If xcplite is installed via a package manager (e.g., `apt install libxcplite-dev`), it's typically in standard system locations and CMake will find it automatically:

```bash
cmake -B build -S .
cmake --build build
```




## See Also

- [Building XCPlite](../../docs/BUILDING.md) - Detailed build instructions
- [XCPlite API Reference](../../docs/xcplite.md) - API documentation
- [hello_xcp](../hello_xcp/) - Basic XCP example with source build
- [XCPlite README](../../README.md) - Project overview
