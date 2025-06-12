#!/bin/bash
# Build universal binary for macOS

echo "Building universal binary for llm_ctx..."

# Clean previous builds
make clean

# Build for x86_64
echo "Building for x86_64..."
make CFLAGS="-std=c99 -Wall -Wextra -Werror -Wstrict-prototypes -D_GNU_SOURCE -g -arch x86_64"
mv llm_ctx llm_ctx_x86_64

# Build for arm64  
echo "Building for arm64..."
make clean
make CFLAGS="-std=c99 -Wall -Wextra -Werror -Wstrict-prototypes -D_GNU_SOURCE -g -arch arm64"
mv llm_ctx llm_ctx_arm64

# Create universal binary
echo "Creating universal binary..."
lipo -create llm_ctx_x86_64 llm_ctx_arm64 -output llm_ctx
rm llm_ctx_x86_64 llm_ctx_arm64

# Check result
echo ""
echo "Result:"
file llm_ctx
echo ""
echo "Now both architectures should work!"