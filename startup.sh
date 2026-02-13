#!/bin/bash

# Load environment variables from .env file
if [ -f .env ]; then
    set -a
    source .env
    set +a
fi

cd build
rm -rf *
cmake ..
make
./bot ../config.json