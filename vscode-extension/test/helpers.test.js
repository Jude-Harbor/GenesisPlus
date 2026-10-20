"use strict";

/* Regression tests for compiler selection in the test infrastructure. */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("fs");
const os = require("os");
const path = require("path");

const { selectCompiler, nativeCompilerName, ROOT } = require("./helpers");

function tempRoot(files) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "genesis-select-"));
    for (const name of files) {
        fs.writeFileSync(path.join(root, name), "not a real compiler");
    }
    return root;
}

test("native compiler names", () => {
    assert.equal(nativeCompilerName("win32"), "genesis.exe");
    assert.equal(nativeCompilerName("linux"), "genesis");
    assert.equal(nativeCompilerName("darwin"), "genesis");
});

test("a Windows genesis.exe is never selected on Linux or macOS", () => {
    const root = tempRoot(["genesis.exe"]);
    try {
        for (const platform of ["linux", "darwin"]) {
            const result = selectCompiler({}, platform, root);
            assert.equal(result.path, null, platform);
            assert.match(result.reason, /not found/);
        }
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test("a native binary that does not run is rejected, not used", () => {
    const root = tempRoot([nativeCompilerName()]);
    try {
        const result = selectCompiler({}, process.platform, root);
        assert.equal(result.path, null);
        assert.match(result.reason, /does not run/);
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test("GENESIS_BIN that does not exist is an error, with no fallback", () => {
    assert.throws(
        () => selectCompiler({ GENESIS_BIN: path.join(os.tmpdir(), "no-such-genesis-binary") }),
        /does not exist/
    );
});

test("GENESIS_BIN that is not Genesis+ is an error", () => {
    /* Node itself runs, but is not a Genesis+ compiler. */
    assert.throws(() => selectCompiler({ GENESIS_BIN: process.execPath }), /not a working Genesis\+ compiler/);
});

test("GENESIS_BIN takes precedence over the repository binary", (t) => {
    const native = path.join(ROOT, nativeCompilerName());
    if (!fs.existsSync(native)) {
        t.skip("compiler not built");
        return;
    }
    /* Copy the real compiler somewhere else and point GENESIS_BIN at it. */
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "genesis bin with spaces "));
    const copy = path.join(dir, nativeCompilerName());
    fs.copyFileSync(native, copy);
    fs.chmodSync(copy, 0o755);
    try {
        const result = selectCompiler({ GENESIS_BIN: copy });
        assert.equal(result.path, copy);
        assert.match(result.version, /^Genesis\+ /);
    } finally {
        fs.rmSync(dir, { recursive: true, force: true });
    }
});
