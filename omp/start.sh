#!/bin/sh
# Starts the open.mp test server (32-bit Linux). Keep stdin open: the server's console reads it.
cd "$(dirname "$0")" && exec ./omp-server
