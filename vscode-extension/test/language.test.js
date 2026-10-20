"use strict";

/*
 * Cross-checks the extension's language table, grammar and snippets against
 * the real compiler, so the editor never suggests syntax the compiler
 * rejects. Needs a built compiler (repo root, or $GENESIS_BIN).
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("fs");
const os = require("os");
const path = require("path");

const { ELEMENTS, PROPERTIES, PADDING_SIDES, CHILD_ELEMENTS } = require("../lib/language");
const { findCompiler, check, expandSnippet } = require("./helpers");

const EXTENSION = path.resolve(__dirname, "..");
const compiler = findCompiler();
const needsCompiler = { skip: compiler ? false : "compiler not built (run make, or set GENESIS_BIN)" };

/* Builds a minimal valid statement for `element` with one extra property. */
function statement(element, property) {
    const values = {
        title: '"T"', background: '"#fff"', color: '"#000"', size: "20", level: "2",
        align: '"center"', gap: "8", width: "50%", padding: "8", to: '"a.html"', alt: '"A"',
    };
    const extra = property ? ` ${property} = ${values[property]}` : "";
    switch (element) {
    case "page": return `page P${extra} { text "x" }`;
    case "column":
    case "row": return `page P { ${element}${extra} { text "x" } }`;
    case "link":
    case "button": return `page P { ${element} "x"${property === "to" ? "" : ' to = "a.html"'}${extra} }`;
    case "image": return `page P { image "https://example.com/a.png"${property === "alt" ? "" : ' alt = "A"'}${extra} }`;
    default: return `page P { ${element} "x"${extra} }`;
    }
}

test("every listed property is accepted by the compiler", needsCompiler, () => {
    for (const [element, spec] of Object.entries(ELEMENTS)) {
        for (const property of spec.properties) {
            const source = statement(element, property);
            const result = check(compiler, source);
            assert.equal(result.code, 0, `${element}.${property} rejected: ${source}\n${result.output}`);
        }
    }
});

test("properties not listed for an element are rejected", needsCompiler, () => {
    for (const [element, spec] of Object.entries(ELEMENTS)) {
        for (const property of Object.keys(PROPERTIES)) {
            if (spec.properties.includes(property)) {
                continue;
            }
            const source = statement(element, property);
            const result = check(compiler, source);
            assert.equal(result.code, 1, `${element}.${property} unexpectedly accepted: ${source}`);
            assert.match(result.output, /cannot be used on/);
        }
    }
});

test("padding sides are accepted", needsCompiler, () => {
    for (const side of PADDING_SIDES) {
        const result = check(compiler, `page P { column padding ${side} = 4 { text "x" } }`);
        assert.equal(result.code, 0, side);
    }
});

test("child elements are valid inside blocks", needsCompiler, () => {
    for (const element of CHILD_ELEMENTS) {
        assert.equal(check(compiler, statement(element, null)).code, 0, element);
    }
});

test("every snippet compiles", needsCompiler, () => {
    const snippets = JSON.parse(fs.readFileSync(path.join(EXTENSION, "snippets", "genesis.json"), "utf8"));
    /* A project folder containing the image the Image snippet refers to. */
    const project = fs.mkdtempSync(path.join(os.tmpdir(), "genesis-snippets-"));
    fs.mkdirSync(path.join(project, "assets"));
    fs.writeFileSync(path.join(project, "assets", "photo.jpg"), "");
    try {
        for (const [name, snippet] of Object.entries(snippets)) {
            let source = expandSnippet(snippet.body);
            if (!source.startsWith("page")) {
                source = elementWrap(source);
            }
            const result = check(compiler, source, project);
            assert.equal(result.code, 0, `snippet ${name} does not compile:\n${source}\n${result.output}`);
        }
    } finally {
        fs.rmSync(project, { recursive: true, force: true });
    }
});

function elementWrap(source) {
    /* An empty block is only a warning, but give it content anyway. */
    return `page P {\n${source.replace(/\{\s*\n\s*\n?\}/, '{ text "x" }')}\n}`;
}

test("the grammar knows every keyword and property", () => {
    const grammar = fs.readFileSync(path.join(EXTENSION, "syntaxes", "genesis.tmLanguage.json"), "utf8");
    JSON.parse(grammar);
    for (const element of Object.keys(ELEMENTS)) {
        assert.ok(grammar.includes(element), `grammar is missing ${element}`);
    }
    for (const property of Object.keys(PROPERTIES)) {
        assert.ok(grammar.includes(property), `grammar is missing ${property}`);
    }
    for (const side of PADDING_SIDES) {
        assert.ok(grammar.includes(side), `grammar is missing padding side ${side}`);
    }
});

test("grammar regexes are valid", () => {
    const grammar = JSON.parse(fs.readFileSync(path.join(EXTENSION, "syntaxes", "genesis.tmLanguage.json"), "utf8"));
    const visit = (node) => {
        if (Array.isArray(node)) {
            node.forEach(visit);
        } else if (node && typeof node === "object") {
            for (const key of ["match", "begin", "end"]) {
                if (typeof node[key] === "string") {
                    /* Oniguruma and JavaScript agree on the constructs used here. */
                    assert.doesNotThrow(() => new RegExp(node[key]), node[key]);
                }
            }
            Object.values(node).forEach(visit);
        }
    };
    visit(grammar);
});

test("manifest files are consistent", () => {
    const manifest = JSON.parse(fs.readFileSync(path.join(EXTENSION, "package.json"), "utf8"));
    assert.match(manifest.version, /^\d+\.\d+\.\d+$/);
    for (const relative of [
        manifest.main,
        manifest.contributes.languages[0].configuration,
        manifest.contributes.grammars[0].path,
        manifest.contributes.snippets[0].path,
        manifest.contributes.iconThemes[0].path,
        manifest.contributes.languages[0].icon.light,
        manifest.icon,
    ]) {
        assert.ok(fs.existsSync(path.join(EXTENSION, relative)), `missing ${relative}`);
    }
    JSON.parse(fs.readFileSync(path.join(EXTENSION, "language-configuration.json"), "utf8"));

    const commands = manifest.contributes.commands.map((c) => c.command);
    const source = fs.readFileSync(path.join(EXTENSION, "extension.js"), "utf8");
    for (const command of commands) {
        assert.ok(source.includes(`"${command}"`), `command ${command} is not registered`);
    }
});
