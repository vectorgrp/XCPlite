cmake_minimum_required(VERSION 3.1.0)
project(xcpLite)

set(SOURCES main.cpp ecupp.cpp ecu.c xcpAppl.c xcpLite.c xcpTl.c A2L.c xcpSlave.c clock.c platform.c)

add_executable(xcpLite ${SOURCES})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(xcpLite PRIVATE Threads::Threads)
