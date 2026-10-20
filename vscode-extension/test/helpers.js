"use strict";

const childProcess = require("child_process");
const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..", "..");

/** The compiler file name for the current platform. */
function nativeCompilerName(platform = process.platform) {
    return platform === "win32" ? "genesis.exe" : "genesis";
}

/** Runs `<candidate> --version` and returns the output if it is Genesis+. */
function probeCompiler(candidate) {
    const result = childProcess.spawnSync(candidate, ["--version"], {
        encoding: "utf8",
        timeout: 10000,
    });
    if (result.error || result.status !== 0 || !/^Genesis\+ \d+\.\d+\.\d+/.test(result.stdout)) {
        const reason = result.error ? result.error.message : `exit status ${result.status}`;
        return { ok: false, reason };
    }
    return { ok: true, version: result.stdout.trim() };
}

/**
 * Selects the compiler used by the tests.
 *
 * 1. If GENESIS_BIN is set, it is used. If it is missing or does not run,
 *    this throws: an explicit choice never falls back to something else.
 * 2. Otherwise the native binary in the repository root is used
 *    (genesis.exe on Windows, genesis elsewhere). A binary built for another
 *    platform, such as genesis.exe on Linux, is never selected.
 *
 * Returns { path, version } or { path: null, reason } when no usable
 * compiler exists (the compiler-backed tests are then skipped, visibly).
 */
function selectCompiler(env = process.env, platform = process.platform, root = ROOT) {
    if (env.GENESIS_BIN) {
        const explicit = path.resolve(env.GENESIS_BIN);
        if (!fs.existsSync(explicit)) {
            throw new Error(`GENESIS_BIN points to '${explicit}', which does not exist.`);
        }
        const probe = probeCompiler(explicit);
        if (!probe.ok) {
            throw new Error(`GENESIS_BIN '${explicit}' is not a working Genesis+ compiler (${probe.reason}).`);
        }
        return { path: explicit, version: probe.version };
    }

    const candidate = path.join(root, nativeCompilerName(platform));
    if (!fs.existsSync(candidate)) {
        return { path: null, reason: `${candidate} not found; run make or set GENESIS_BIN` };
    }
    const probe = probeCompiler(candidate);
    if (!probe.ok) {
        return { path: null, reason: `${candidate} does not run (${probe.reason}); rebuild with make` };
    }
    return { path: candidate, version: probe.version };
}

let selected;

/** The selected compiler path, or null. Selection happens once. */
function findCompiler() {
    if (selected === undefined) {
        selected = selectCompiler();
        if (selected.path) {
            console.log(`# Using compiler: ${selected.path} (${selected.version})`);
        } else {
            console.log(`# Compiler-backed tests skipped: ${selected.reason}`);
        }
    }
    return selected.path;
}

/** Runs `genesis check --stdin --machine` on `source`. */
function check(compiler, source, cwd) {
    const result = childProcess.spawnSync(compiler, ["check", "--stdin", "--machine"], {
        cwd,
        input: source,
        encoding: "utf8",
        timeout: 10000,
    });
    return { code: result.status, output: result.stdout + result.stderr };
}

/** Replaces snippet placeholders (${1:x}, ${2|a,b|}, $0, \t) with plain text. */
function expandSnippet(lines) {
    return lines
        .join("\n")
        .replace(/\$\{\d+\|([^,|]*)[^}]*\|\}/g, "$1")
        .replace(/\$\{\d+:([^}]*)\}/g, "$1")
        .replace(/\$\{\d+\}/g, "")
        .replace(/\$\d+/g, "")
        .replace(/\t/g, "    ");
}

module.exports = { ROOT, nativeCompilerName, probeCompiler, selectCompiler, findCompiler, check, expandSnippet };
