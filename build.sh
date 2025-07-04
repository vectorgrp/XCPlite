
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build  

# Be sure EPK is updated before building
touch src/a2l.c

make --directory ./build


