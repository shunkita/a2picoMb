# Project Rules and Guidelines for a2picoMb

## Language Rules (Crucial)

1. **User Interaction & Chat**:
   - Respond to the user in **Japanese** (日本語で応答すること).

2. **Source Code, Comments, and Documentation (STRICT - ENGLISH ONLY)**:
   - **ALL comments, docstrings, identifiers, log/debug messages, and documentation within the codebase (C, C++, Assembly, Python, CMake, Shell scripts, etc.) MUST be written in 100% ENGLISH.**
   - **NEVER insert Japanese characters (Kanji, Hiragana, Katakana, full-width characters) into any source code file.**
   - Even if the user prompt or specification contains Japanese explanatory comments or pseudocode, you **MUST translate them into clear, natural, idiomatic English** before inserting them into code files. Never copy-paste Japanese comments into source files.

## Technical Code Guidelines

- **Apple II Bus Read Critical Path**:
  - Core 1 bus read critical path must remain strictly within the budget of **350 ns / 70 cycles (@ 200 MHz)**.
  - Maintain the warning gate threshold of **50 cycles (250 ns)**.
- **Verification**:
  - Always run `./build.sh` after modifications to ensure build integrity and timing pass.
- **Git Operations**:
  - Perform local git operations only. Never push to remote.
