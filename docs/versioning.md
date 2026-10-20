# Versioning policy

Genesis+ uses [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`.

**Before 1.0 (the current 0.x series)** the language is in public testing. As Semantic Versioning allows, a *minor* release (0.9 → 0.10) may contain breaking changes; every breaking change is listed in `CHANGELOG.md` with migration instructions. Patch releases (0.9.0 → 0.9.1) never break valid programs. From 1.0 on, the table below applies.

| Change | Version bump | Examples |
| --- | --- | --- |
| A valid program stops compiling, or its output changes in a way that changes how the page looks or behaves | **MAJOR** | removing a property, changing a default color, renaming an element |
| New elements, properties, commands, or options that do not affect existing programs | **MINOR** | adding a `video` element, adding `genesis build --out` |
| Bug fixes, clearer error messages, documentation, performance | **PATCH** | fixing a crash, better hints |

Notes:

- **Error message wording is not part of the stable interface.** It can improve in any release. The `--machine` diagnostic *format* (the tab-separated fields) is stable within a major version.
- **Generated HTML/CSS formatting** (whitespace, attribute order) may change in a minor release as long as pages look and behave the same.
- **The `<meta name="generator">` tag** contains the compiler version, so output changes with every release.
- The compiler and the VS Code extension are currently versioned together. They may diverge later, in which case the extension's changelog will say which compiler versions it supports.

## Where the version lives

- Compiler: `GENESIS_VERSION` in `include/version.h` (the only place).
- VS Code extension: `version` in `vscode-extension/package.json`.
- Changelogs: `CHANGELOG.md` and `vscode-extension/CHANGELOG.md`.

Releases are tagged `vMAJOR.MINOR.PATCH` (for example `v0.9.0`). See [release-checklist.md](release-checklist.md).
