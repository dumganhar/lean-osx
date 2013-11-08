#!/bin/bash
if [ $# -ne 1 ]; then
    echo "Usage: test.sh [leanlua-executable-path]"
    exit 1
fi
ulimit -s unlimited
LEANLUA=$1
NUM_ERRORS=0
for f in `ls *.lua`; do
    echo "-- testing $f"
    if $LEANLUA util.lua $f > $f.produced.out; then
        echo "-- worked"
    else
        echo "ERROR executing $f, produced output is at $f.produced.out"
         NUM_ERRORS=$(($NUM_ERRORS+1))
    fi
done
if [ $NUM_ERRORS -gt 0 ]; then
    echo "-- Number of errors: $NUM_ERRORS"
    exit 1
else
    echo "-- Passed"
    exit 0
fi