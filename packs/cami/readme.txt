CAMI LANGUAGE PACK VERIFICATION

The Cami language pack is designed to parse JavaScript and TypeScript files that use the Cami.js framework. 
Here's how to verify that it works correctly:

1. INSTALL TREE-SITTER GRAMMARS
   a) Install tree-sitter: brew install tree-sitter
   b) Clone the JavaScript grammar:
      git clone https://github.com/tree-sitter/tree-sitter-javascript
   c) Clone the TypeScript grammar:
      git clone https://github.com/tree-sitter/tree-sitter-typescript
   d) Build both grammars following their documentation

2. SET UP THE PACK
   a) Create a 'packs' directory in the location where llm_ctx looks for packs
      (Typically /usr/local/share/llm_ctx/packs or ~/.local/share/llm_ctx/packs)
   b) Copy the Cami pack files to a 'cami' subdirectory:
      - cami_pack.c
      - tree-sitter.h
      - Makefile
      - README.md

3. UPDATE THE MAKEFILE
   a) Edit the Makefile to point to your built tree-sitter grammar libraries
   b) Ensure the shared library output is named 'parser.so'

4. BUILD THE PARSER
   a) Run 'make' in the 'cami' directory to build parser.so

5. VERIFY THE PACK IS LOADED
   a) Run 'llm_ctx --list-packs'
   b) You should see 'cami' listed as an available pack
   c) Run 'llm_ctx --pack-info cami' to see details about the pack

6. TEST WITH A CAMI.JS FILE
   a) Create a simple test file (test.js) with Cami.js code
   b) Run 'llm_ctx -f test.js -m' to generate a codemap
   c) The output should show extracted entities like:
      - Stores
      - Actions
      - AsyncActions
      - Queries
      - Mutations
      - Memos
      - Components

When properly integrated with llm_ctx, this pack will provide detailed codebase insights for Cami.js applications.