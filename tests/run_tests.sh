#!/bin/sh

# Source file to build and run.

SRCFILE_LIBS="-lGL -lX11 -pthread"
SRCFILE=run_tests.cpp

# Include the compile_and_run_func function.
. $(dirname $(readlink -f "$0"))/../bin/src/compile_and_run.sh

# Tell it to compile and run.
compile_and_run_func "$@"
