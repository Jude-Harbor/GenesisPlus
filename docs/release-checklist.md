# Release checklist — Genesis+ v0.9.0

Status: **ready for final review, not released.** Nothing has been committed, pushed, tagged, or published.

Local verification ran on 2026-10-20 on Windows 10 with MSYS2 UCRT64 GCC 16.2.0 and Node.js 22.

## 1. Verified locally

Each item below was run and its result observed.

- [x] `make clean` (exit 0) removes `genesis.exe` and `build/`.
- [x] `make` (exit 0): strict C17 build (`-std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion ...`) with 0 warnings.
- [x] `make test` from a clean tree (exit 0):
  - line-ending check: 91 text files, all LF;
  - unit, golden-file and fuzz tests: 31,117 checks;
  - CLI and dev-server tests: 179 checks, run in a folder whose path contains spaces, and including every `gns` code block in the docs;
  - VS Code extension tests: 25/25, run against the freshly built compiler via `GENESIS_BIN`.
- [x] `make test` stops with a non-zero exit (2) when a test fails (checked with a deliberately corrupted golden file).
- [x] `make examples` (exit 0): all 6 examples build; their `lead.gns` sources are unchanged afterwards.
- [x] `make analyze` (exit 0). Three `-Wanalyzer-fd-*` warnings in `src/server.c` were re-examined and are false positives: the analyzer treats Winsock `SOCKET` values as POSIX file descriptors. See `docs/architecture.md`.
- [x] `make install PREFIX=<repo>/build/stage` (exit 0); the installed binary reports `Genesis+ 0.9.0`.
- [x] `GENESIS_BIN` selects the given compiler. A missing or non-Genesis+ `GENESIS_BIN` makes the extension tests fail (exit 1) instead of falling back to another binary.
- [x] CRLF guard: a file with CRLF makes `sh tests/check_line_endings.sh` exit 1. A throwaway clone with `core.autocrlf=true` contained no CR characters.
- [x] Generated binaries, `build/` and every `dist/` are ignored by git and would not be committed.
- [x] Dev server: binds to `127.0.0.1` only (a connection via the LAN address is refused). Path traversal, device names, bad encodings, oversized requests and non-GET methods are rejected (CLI tests). Ctrl+C shutdown works on Windows. Live reload and phone-width layout were checked in a real browser.
- [x] Extension: a failing compiler is reported, not shown as a clean file (automated test).
- [x] VS Code, viewing-only checks in an Extension Development Host:
  - `.gns` language mode, file icon and highlighting;
  - error underline and Problems entry at the correct column on a line with an emoji;
  - hover help.
- [x] Version is `0.9.0` everywhere: `include/version.h`, `genesis --version`, help text, both changelogs, release notes, `vscode-extension/package.json`, golden files.
- [x] MIT license: `LICENSE` and `vscode-extension/LICENSE` are identical; `vscode-extension/package.json` has `"license": "MIT"`.
- [x] Contact address set in `CODE_OF_CONDUCT.md`, `SECURITY.md` and the README.

## 2. Requires your input

- [ ] **Copyright holder.** Replace `[COPYRIGHT HOLDER — TO BE PROVIDED]` in both `LICENSE` and `vscode-extension/LICENSE` with the legal name (person or organisation) that holds the copyright. Keep both files identical. Adjust the year if needed (currently 2026).
- [ ] **GitHub repository URL.** After creating the repository, add it to `vscode-extension/package.json`:
  `"repository": { "type": "git", "url": "https://github.com/<owner>/<repo>.git" }`.
  It is deliberately absent now rather than filled with a guess. `vsce package` warns without it.
- [ ] **Marketplace publisher.** `vscode-extension/package.json` uses `"publisher": "judeharbor"`, carried over from the original project. Confirm you own that publisher ID before publishing the extension.

## 3. Requires external verification

- [ ] **CI on Windows, Linux and macOS** (`.github/workflows/ci.yml`). It has never run, because there is no remote. Until it passes, Linux and macOS builds are unverified. The workflow file was reviewed by eye; it was not machine-validated locally (no YAML parser was available).
- [ ] **Sanitizers** (`make sanitize`, AddressSanitizer + UBSan). These are unavailable with MSYS2 GCC, where `make sanitize` exits 2 with an explanation. They run in the Linux CI job.
- [ ] **Remaining VS Code checks**, which need a person at the keyboard (press F5 in the repository):
  - [ ] completion offers elements at the start of a line and only valid properties after an element; snippets expand;
  - [ ] *Format Document* formats a valid file and leaves a file with errors unchanged;
  - [ ] errors update while typing;
  - [ ] *Genesis+: Build Website*, *Start Dev Server* and *Stop Dev Server* work, with output in the *Genesis+* channel;
  - [ ] with the compiler missing, one warning is shown and highlighting still works;
  - [ ] in an untrusted folder (Restricted Mode), a `genesis.exe` inside the folder is not run;
  - [ ] a `:` is highlighted as an error.
- [ ] **VSIX packaging and installation.** `@vscode/vsce` is not installed and was not downloaded. Run `npx @vscode/vsce package` in `vscode-extension/`, then install the `.vsix` with *Extensions: Install from VSIX...* and repeat a quick check.

## 4. Final release actions (future steps; do not run until the items above are done)

1. Fill in the placeholders from section 2 and run `make clean && make test` again.
2. Review and commit:
   ```bash
   git status
   git add -A
   git status            # confirm no .exe, build/ or dist/ files are listed
   git commit -m "Genesis+ 0.9.0 public testing release"
   ```
3. Create an empty GitHub repository and connect it:
   ```bash
   git remote add origin https://github.com/<owner>/<repo>.git
   git push -u origin main
   ```
4. Open the repository's **Actions** tab and wait for all three jobs (Linux — which also runs the sanitizer, analyzer and install steps — macOS, and Windows) to pass. Fix anything platform-specific in a new commit.
5. Complete the VS Code checks in section 3 and the VSIX test.
6. Tag and push the tag:
   ```bash
   git tag -a v0.9.0 -m "Genesis+ 0.9.0"
   git push origin v0.9.0
   ```
7. Create the GitHub release from the tag using `docs/release-notes-v0.9.0.md`. Attach binaries only if they were built from the tagged commit (`genesis.exe` from MSYS2 UCRT64; platform-named `genesis` builds for Linux and macOS).
8. Optional: publish the extension with `npx @vscode/vsce publish`, once the publisher ID is confirmed.

Share source with `git archive` or the GitHub release, not by zipping the working folder: a zip of the folder includes local build output such as `genesis.exe` and `build/`.

## Every later release

1. Update `GENESIS_VERSION` in `include/version.h` and `version` in `vscode-extension/package.json`.
2. Move the *Unreleased* changelog entries (root and extension) under the new version.
3. If output changed intentionally: `make build/unit_tests`, then `GENESIS_UPDATE_GOLDEN=1 ./build/unit_tests` (`.exe` on Windows), and review `git diff tests/golden`.
4. `make clean && make test && make examples && make analyze`; on Linux or macOS also `make sanitize`.
5. Push, wait for CI, tag, release.
