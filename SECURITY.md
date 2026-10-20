# Security policy

## Reporting a vulnerability

Please do not report security problems in public issues. Email the maintainers privately at [Jmcustoms1609@gmail.com](mailto:Jmcustoms1609@gmail.com). Once the project is on GitHub, private vulnerability reporting can also be enabled for the repository.

Include the Genesis+ version (`genesis --version`), your operating system, and the smallest `.gns` file or request that shows the problem.

## What counts as a security issue

- A `.gns` file that makes the compiler crash, hang, corrupt memory, or read or write files outside the project folder and its `dist/` folder.
- Generated HTML that runs script or loads unexpected content from text in a `.gns` file (for example, escaping failures or `javascript:` links getting through).
- The dev server (`genesis dev`) serving files outside `dist/`, or being reachable from other computers.
- The VS Code extension running a program from an untrusted folder.

## Design notes

- The compiler never runs other programs and makes no network connections.
- The dev server listens only on `127.0.0.1` and serves only files inside `dist/`.
- All user text is HTML-escaped. Only validated numbers and colors are written into CSS.
- Links may only use relative addresses or the `http`, `https`, `mailto` and `tel` schemes.
