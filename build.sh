
# ccmake -B build -S .

# Debug build with Clang
# Use gcc for raspberry pi builds, if problems with atomic_uint_least32_t
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build -DUSE_CLANG=OFF

# Release build with Gcc
# cmake -DCMAKE_BUILD_TYPE=Release -S . -B build -DUSE_CLANG=OFF

# Be sure EPK is updated before building
touch src/a2l.c

make --directory ./build


