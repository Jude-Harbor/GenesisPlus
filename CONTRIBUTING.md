# Contributing to Genesis+

Thank you for helping. Genesis+ aims to stay small, predictable and friendly to beginners, so please read the design goals before proposing new features.

## Design goals

1. Simple, readable syntax.
2. Structure from braces, never from indentation.
3. Code copied from anywhere (including AI assistants) should just work.
4. Error messages that explain what went wrong and how to fix it.
5. Sensible defaults, so simple sites need little code.
6. A small language surface: a feature must be useful for real websites and fully implemented, tested and documented.
7. Deterministic output and no unnecessary dependencies.

New elements or properties should come with an issue explaining the use case first.

## Getting started

Follow the build instructions in the [README](README.md). Then:

```bash
make            # build
make test       # the release gate: line endings, unit, CLI, dev server, docs examples,
                # and extension tests (needs a POSIX shell, curl, and Node.js 18+)
make examples   # compile the examples
make analyze    # static analysis
```

On Linux and macOS, please also run `make sanitize` before submitting C changes.

The extension tests are part of `make test` (`make test-extension` runs them alone). They use the compiler you just built; to test another binary set `GENESIS_BIN` to its path. An invalid `GENESIS_BIN` fails the tests rather than silently using a different compiler.

Press F5 in VS Code to launch an Extension Development Host.

## Code style

- ISO C17, no compiler extensions except where guarded (the `GENESIS_PRINTF` format attribute). Operating-system code belongs in `src/platform.c` or `src/server.c` only.
- The build must stay free of warnings with the flags in the `Makefile` (including `-Wconversion`).
- Follow the existing style: 4-space indentation, braces on the same line for control flow, small functions, comments that explain *why*.
- Follow the memory ownership rules in [docs/architecture.md](docs/architecture.md).
- `.gns` files should be formatted with `genesis fmt`.
- All text files use LF line endings. `.gitattributes` enforces this on checkout, and `make check-eol` (part of `make test`) fails on CRLF. Configure your editor to keep LF (VS Code does this automatically for existing LF files).

## Tests

Every bug fix needs a test that fails without the fix. Put it where it fits best:

| Change | Test location |
| --- | --- |
| Tokens | `tests/unit/test_lexer.c` |
| Grammar, validation, error messages | `tests/unit/test_parser.c` |
| HTML/CSS output | `tests/unit/test_codegen.c` and, for larger outputs, `tests/golden/` |
| Formatter | `tests/unit/test_formatter.c` |
| Dev-server paths | `tests/unit/test_server.c` |
| Command-line behaviour, files on disk | `tests/cli_test.sh` |
| Editor features | `vscode-extension/test/` |

Golden files are updated with `GENESIS_UPDATE_GOLDEN=1 ./build/unit_tests` (`build/unit_tests.exe` on Windows). Always review the resulting diff.

When you change the language, update `docs/language-reference.md`, `vscode-extension/lib/language.js`, and the grammar in `vscode-extension/syntaxes/`. Code blocks marked `gns` in the documentation are compiled by the test suite.

## Pull requests

- Keep each pull request focused on one change.
- Describe what changed and why, and how you tested it.
- Add an entry to `CHANGELOG.md` under *Unreleased*.
- Make sure CI passes.

## License

Genesis+ is MIT licensed (see [LICENSE](LICENSE)). By submitting a contribution you agree that it is provided under the same license.

## Code of conduct

Everyone taking part in this project is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
