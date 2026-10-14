#!/bin/bash
# Get status of SHM mode applications

./build/shmtool status -v

bintool --bin main.bin --dump --verbose 
