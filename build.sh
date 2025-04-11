#!/bin/bash
# TODO: use a build system

mkdir -p tmp
mkdir -p bin

# compile logging library
gcc -c -Wall -Werror -o tmp/logging.o logging.c

# compile vswitch
gcc -c -Wall -Werror -o tmp/vswitch.o vswitch.c

# compile vport
gcc -c -Wall -Werror -o tmp/vport.o vport.c

# link vswitch
gcc -o bin/vswitch tmp/vswitch.o tmp/logging.o

# link vport
gcc -o bin/vport tmp/vport.o tmp/logging.o

# compile  & link shell
gcc -Wall -Werror -o bin/vswsh vswsh.c

rm -fr tmp