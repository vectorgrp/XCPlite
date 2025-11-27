# Building XCPlite

## Requirements

- **C Standard:** C11
- **C++ Standard:** C++20 (for C++ support)
- **Platforms:** Linux, macOS, QNX, Windows (with limitations)

Most of the examples require **CANape 23 or later**, because they use A2L TYPEDEFs and relative memory addressing.

## Quick Build

### Linux or macOS

Use the build script to build the library xcplib, example targets and get comprehensive output on issues:

```bash
./build.sh
```

Or build individual example targets:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build  
make --directory ./build hello_xcp
```

### QNX 

Building QNX targets requires the QNX Software Development Platform (SDP) to be installed on the host.
The installation directory of the QNX SDP to be used for compilation must be given as input argument to the build script.
Note that all CPP targets are currently excluded from the build if QNX SDP 7.0 or lower is used, due to missing support of std::optional.
Currently, two target architectures are supported: x86_64 and aarch64le

Build all suitable targets with QNX 7.1.0 for x86_64 platforms on a Windows host:

```bash
build_qnx.bat Debug "C:\QNX\qnx710" x86_64
```

Build all suitable targets with QNX 8.0.0 for AArch64 platforms on a Linux host:

```bash
 ./build.sh Debug qcc all -q=/home/qnx800 -a=aarch64le
```

### Windows

It is possible to build for Windows with the Microsoft Visual Studio compiler, but there are some limitations and performance penalties under Windows.  
XCPlite is optimized for Posix based systems.  
On Windows, atomic operations are emulated and the transmit queue always uses a mutex on the producer side.

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build-msvc
cmake --build build-msvc --target hello_xcp
build-msvc/debug/hello_xcp.exe
```

To create a Visual Studio solution:

```bash
./build.bat
```

## Troubleshooting Compilation Issues

First of all, note that XCPlite requires C11 (and C++17 for C++ support).

A possible problematic requirement is that the 64-bit lockless transmit queue implementation requires `atomic_uint_least64_t`.  
This may cause problems on some platforms when using the clang compiler.  
**Prefer gcc for better compatibility.**  
If this is not an option, the mutex based 32-bit queue may be used instead.

### Testing Different Compilers

```bash
# Test with system default
cmake -B build -S . && cmake --build build

# Test with clang
cmake -B build-clang -S . -DUSE_CLANG=ON && cmake --build build-clang

# Test with GCC  
cmake -B build-gcc -S . -DUSE_GCC=ON && cmake --build build-gcc
```

### Using build.sh for Diagnostics

There is a script `build.sh` to automate the build process and to check which of the targets have build issues.  
If there are failures, copy & paste the complete output and provide it.

```bash
./build.sh
```

`build.sh` has command line parameters to select a release or debug build and force the compiler to be used:

```bash
./build.sh --help
```

Default is the system default compiler and debug build.

On failure try to force gcc:

```bash
./build.sh gcc
```

### Type Detection Tests

If build fails and if `type_detection_test_c` builds ok, run it:

```bash
./build/type_detection_test_c.out
```

Copy & paste the complete output and provide it.

Same with `type_detection_test_cpp`:

```bash
./build/type_detection_test_cpp.out
```

## Platform-Specific Notes

### Windows Limitations

- Atomic operations are emulated
- The transmit queue always uses a mutex on the producer side
- Overall performance is lower compared to POSIX systems

### Compiler Compatibility

- **Recommended:** GCC (best compatibility with atomic operations)
- **Alternative:** Clang (may have issues with `atomic_uint_least64_t` on some platforms)
- **Windows:** MSVC (with limitations noted above)
