#!/bin/sh
# Starts the SA-MP test server (32-bit Linux). Keep stdin open: the server reads its console from it.
cd "$(dirname "$0")" && exec ./samp03svr
