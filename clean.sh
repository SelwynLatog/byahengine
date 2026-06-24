#!/bin/bash
# Wipes build output and all generated asset caches for a clean rebuild
rm -rf build/
find assets/ -name "*.texcache" -o -name "*.objcache" | xargs rm -f
echo "Cache cleaned."
echo "Run ./run.sh to build with clean state."