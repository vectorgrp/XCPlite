#!/bin/bash
# Clean SHM mode artefacts



./build/shmtool clean

rm *.a2l
rm *.bin

./build.sh cleanall



