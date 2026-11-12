#!/bin/env sh
gcc src/lambda.c src/parse.c src/interp.c dep/alloc.c -o bin/bruijn -O3 -ffast-math -nostdlib -fno-builtin -Wall -Wextra -Wno-builtin-declaration-mismatch -march=native -static $@
