#!/bin/bash
# TODO: use a build system

mkdir -p tmp
mkdir -p bin

# compile logging library
gcc -c -o tmp/logging.o logging.c

# compile vswitch
gcc -c -o tmp/vswitch.o vswitch.c

# compile vport
gcc -c -o tmp/vport.o vport.c

# link vswitch
gcc -o bin/vswitch tmp/vswitch.o tmp/logging.o

# link vport
gcc -o bin/vport tmp/vport.o tmp/logging.o

rm -fr tmp