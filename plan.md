# README.md Restructuring Plan (Diátaxis Framework)

**Goal:** Create a more organized, user-centric README that helps users learn, solve specific problems, look up details, and understand the concepts behind `llm_ctx`.

**Diátaxis Structure:**

1.  **Tutorials:** Learning-oriented, guiding the user through initial steps.
2.  **How-To Guides:** Goal-oriented, providing steps to achieve specific outcomes.
3.  **Reference:** Information-oriented, providing technical descriptions.
4.  **Explanation:** Understanding-oriented, clarifying concepts and context.

---

**Algorithmic Plan for README Restructuring:**

1.  **Header & Overview:**
    *   Keep the project title (`# llm_ctx`).
    *   Add any relevant badges (build status, license) if available/desired.
    *   Write a concise **Overview** paragraph explaining *what* `llm_ctx` is (formats code/text for LLMs), *who* it's for (developers), and the *key benefit* (structured context, respects `.gitignore`, integrates with workflows).
    *   **(Optional but Recommended):** Add a single, compelling **Quick Example** demonstrating a common powerful use case (e.g., `git diff | llm_ctx -c "Review" | pbcopy`).
    *   Add **Quick Links** section pointing to the main Diátaxis sections (Tutorials, How-To Guides, Reference, Explanation).

2.  **Installation:**
    *   Move the existing Installation section here, keeping it prominent.
    *   Ensure prerequisites, build steps (`make`), and installation options (`make install`, `make symlink`) are clear.

3.  **Tutorials Section:**
    *   Create a `## Tutorials` heading.
    *   Purpose: Guide a new user from zero to basic proficiency.
    *   Content:
        *   **Getting Started:** Briefly reiterate the core idea (piping or using `-f`).
        *   **Analyzing Your First File:** A simple step-by-step guide:
            1.  Create a dummy file (e.g., `echo "hello world" > test.txt`).
            2.  Run `llm_ctx -f test.txt`.
            3.  Explain the basic output structure seen (`<file_tree>`, `<file_context>`).
        *   **Analyzing Git Changes:** Show the most common stdin use case:
            1.  Make a change in a git repo.
            2.  Run `git diff | llm_ctx`.
            3.  Show how to add instructions: `git diff | llm_ctx -c "Explain these changes"`.
        *   **Piping to Clipboard:** Briefly mention this common workflow step (`| pbcopy`, `| xclip`, `| clip`).

4.  **How-To Guides Section:**
    *   Create a `## How-To Guides` heading.
    *   Purpose: Provide clear steps for common, specific tasks. Frame titles as "How to...".
    *   Content (Examples):
        *   **How to Analyze Specific Files:** `llm_ctx -f path/to/file1.c path/to/another/file2.js`
        *   **How to Analyze Files Using Patterns:**
            *   Simple glob: `llm_ctx -f 'src/*.c'` (mention quoting for shell).
            *   Recursive glob: `llm_ctx -f 'src/**/*.js'`
        *   **How to Analyze Content Piped from Other Commands:** `cat report.json | llm_ctx`, `git show HEAD:main.c | llm_ctx`
        *   **How to Exclude Files/Directories (Using `.gitignore`):**
            1.  Explain that `llm_ctx` reads `.gitignore` by default.
            2.  Show an example: Add `*.log` or `build/` to `.gitignore`.
            3.  Run `llm_ctx -f '**/*'` – logs/build files will be skipped.
        *   **How to Include Files That Are Normally Ignored:** Use `--no-gitignore`: `llm_ctx -f --no-gitignore config.log 'src/**/*.c'`
        *   **How to Add Instructions for the LLM:** Use `-c`: `llm_ctx -f main.c -c "Focus on the main function"`
        *   **How to Send Output to Clipboard:** Provide specific commands for macOS, Linux, Windows.
        *   **How to Combine Different Sources (Advanced):** Show shell techniques: `{ git diff; find src -name "*.py" | xargs cat; } | llm_ctx -c "Review diff and Python files"`

5.  **Reference Section:**
    *   Create a `## Reference` heading.
    *   Purpose: Provide detailed, accurate technical information.
    *   Content:
        *   **Command-Line Options:** List all options (`-f`, `-c`, `-h`, `--no-gitignore`) with detailed descriptions of their behavior.
        *   **Input Methods:** Describe behavior with stdin (auto-detecting content vs. file lists - *Note: current implementation seems to favor content*) vs. file arguments (`-f`). Explain the `isatty` check.
        *   **Output Format:** Detail the structure:
            *   `<user_instructions>` (optional)
            *   `<file_tree>` (structure, root, indentation)
            *   `<file_context>` (wrapper)
            *   `File: <filepath>` header
            *   ```` ```[type] ```` fenced blocks (mention type detection for stdin)
            *   `----------------------------------------` separator
        *   **Glob Pattern Support:** Explain which patterns are supported (`*`, `?`, `[]`, `**`). Mention reliance on `glob()` and custom handling for `**`. Note potential shell expansion vs. internal expansion.
        *   **`.gitignore` Integration:** Explain how it works: reads `.gitignore` from current and parent dirs, respects standard rules (including `!`), precedence (later lines/deeper files override), disabled by `--no-gitignore`.
        *   **Installation Details:** (Can be brief, linking back to the main Installation section if desired, or repeat details).
        *   **Testing:** How to run tests (`make test`).

6.  **Explanation Section:**
    *   Create a `## Explanation` heading.
    *   Purpose: Provide context, rationale, and deeper understanding.
    *   Content:
        *   **Project Philosophy:** Why `llm_ctx` exists (Unix philosophy, structured LLM input, workflow integration).
        *   **File Selection and Filtering:** Explain the *difference* from tools with explicit `--include`/`--exclude`. Emphasize the reliance on input arguments for inclusion and `.gitignore` for exclusion. Discuss the trade-offs (simplicity, Git integration vs. ad-hoc flexibility).
        *   **Understanding the Output Structure:** Explain *why* the XML-like tags, file tree, and fenced blocks are used (clarity for LLMs, context preservation, easy parsing).
        *   **Tips for Effective LLM Context:** Best practices (select relevant files, use `-c` effectively, consider token limits - though `llm_ctx` doesn't manage this directly).
        *   **Compatibility:** List tested/expected platforms (macOS, Linux, WSL).

7.  **License Section:**
    *   Keep the existing License information (e.g., `## License\n\nMIT`).

8.  **Review and Refine:**
    *   Read through the entire restructured README.
    *   Check for clarity, accuracy, and consistency.
    *   Ensure examples are correct and easy to understand.
    *   Verify internal links (Quick Links) work.
    *   Use Markdown formatting effectively (headings, code blocks, lists, bolding).
