
# ccmake -B build -S .

# Debug build with Clang
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build -DUSE_CLANG=ON

# Release build with GCC
# cmake -DCMAKE_BUILD_TYPE=Release -S . -B build -DUSE_CLANG=OFF

# Be sure EPK is updated before building
touch src/a2l.c

make --directory ./build


