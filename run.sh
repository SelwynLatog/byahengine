#!/bin/bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
if [ $? -eq 0 ]; then
    echo "OK"
    ./byahengine
else
    echo "FAILED"
fi