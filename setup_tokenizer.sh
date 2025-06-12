#!/bin/bash
# Setup script for llm_ctx with tokenizer support

echo "Setting up llm_ctx with tokenizer support..."

# Build everything
echo "Building tokenizer library..."
make clean-tokenizer >/dev/null 2>&1
make tokenizer || { echo "Failed to build tokenizer"; exit 1; }

echo "Building llm_ctx..."
make clean >/dev/null 2>&1
make || { echo "Failed to build llm_ctx"; exit 1; }

# Check architectures
LLM_ARCH=$(file ./llm_ctx | grep -o 'x86_64\|arm64' | head -1)
TOK_ARCH=$(file tokenizer/libtiktoken_c.dylib 2>/dev/null | grep -o 'x86_64\|arm64' | head -1)

if [[ -n "$TOK_ARCH" ]]; then
    echo "✓ Tokenizer built successfully (${TOK_ARCH})"
    echo "✓ llm_ctx built successfully (${LLM_ARCH})"
    
    if [[ "$LLM_ARCH" != "$TOK_ARCH" ]]; then
        echo ""
        echo "⚠️  Architecture mismatch detected!"
        echo "   llm_ctx: ${LLM_ARCH}, tokenizer: ${TOK_ARCH}"
        echo ""
        echo "To use tokenizer features, run llm_ctx with the arch command:"
        echo ""
        if [[ "$TOK_ARCH" == "arm64" ]]; then
            echo "  arch -arm64 ./llm_ctx [options]"
            echo ""
            echo "Or add this alias to your ~/.bashrc or ~/.zshrc:"
            echo "  alias llm_ctx='arch -arm64 $(pwd)/llm_ctx'"
        else
            echo "  arch -x86_64 ./llm_ctx [options]"
            echo ""
            echo "Or add this alias to your ~/.bashrc or ~/.zshrc:"
            echo "  alias llm_ctx='arch -x86_64 $(pwd)/llm_ctx'"
        fi
    else
        echo ""
        echo "✓ Architecture match - tokenizer will work correctly!"
        echo ""
        echo "Usage examples:"
        echo "  ./llm_ctx -f main.c -b 10000  # Enforce 10k token limit"
        echo "  ./llm_ctx -f src/*.js -D       # Show token breakdown"
    fi
else
    echo "✓ llm_ctx built successfully"
    echo "⚠️  Tokenizer not available (optional feature)"
fi

echo ""
echo "Test the installation:"
echo "  echo 'Hello world' | ./llm_ctx -C -D -o"