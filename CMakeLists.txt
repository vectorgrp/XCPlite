cmake_minimum_required(VERSION 3.1.0)
project(xcpLite)

set(SOURCES main.cpp ecupp.cpp ecu.c udpserver.c udp.c xcpAppl.c xcpLite.c A2L.c xcpSlave.c clock.c)
set(CMAKE_BUILD_TYPE Debug)

add_executable(xcpLite ${SOURCES})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(xcpLite PRIVATE Threads::Threads)

