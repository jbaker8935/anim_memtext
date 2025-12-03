#!/bin/bash
# Build wrapper for Anim Memtext
# Run the actual build
echo "Running full build..."
../llvm-mos/f256dev/f256build.sh ../anim_memtext "$@"
