#!/bin/bash
# TODO: use a build system

mkdir -p tmp/vswitch
mkdir -p tmp/util
mkdir -p bin

# compile logging library
gcc -c -Wall -Werror -Iinclude -o tmp/util/logging.o src/util/logging.c

# compile all vswitch modules
gcc -c -Wall -Werror -Iinclude -o tmp/vswitch/core.o src/vswitch/core.c
gcc -c -Wall -Werror -Iinclude -o tmp/vswitch/fib.o src/vswitch/fib.c
gcc -c -Wall -Werror -Iinclude -o tmp/vswitch/vport.o src/vswitch/vport.c
gcc -c -Wall -Werror -Iinclude -o tmp/vswitch/trunk.o src/vswitch/trunk.c
gcc -c -Wall -Werror -Iinclude -o tmp/vswitch/cli.o src/vswitch/cli.c
gcc -c -Wall -Werror -Iinclude -o tmp/vswitch/main.o src/vswitch/main.c

# compile vport
gcc -c -Wall -Werror -Iinclude -o tmp/vport.o src/vport.c

# link vswitch
gcc -o bin/vswitch \
    tmp/util/logging.o \
    tmp/vswitch/core.o \
    tmp/vswitch/fib.o \
    tmp/vswitch/vport.o \
    tmp/vswitch/trunk.o \
    tmp/vswitch/cli.o \
    tmp/vswitch/main.o

# link vport
gcc -o bin/vport tmp/vport.o tmp/util/logging.o

# compile & link shell
gcc -Wall -Werror -o bin/vswsh src/vswsh.c

rm -fr tmp