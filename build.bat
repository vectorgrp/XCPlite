# Create a Visual Studio 17 2022 solution in the build-msvc directory, with examples enabled
cmake -G "Visual Studio 17 2022"  -S . -B build-msvc  -DXCPLITE_BUILD_EXAMPLES=ON
