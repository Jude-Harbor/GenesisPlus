"use strict";

/*
 * Loads extension.js with a minimal mock of the `vscode` module and drives
 * its providers. This does not replace a manual check inside VS Code, but it
 * verifies that activation registers everything and that diagnostics,
 * completion and formatting are wired to the real compiler correctly.
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const Module = require("module");
const path = require("path");

const { findCompiler } = require("./helpers");

const compiler = findCompiler();

function createMock(overrides = {}) {
    const registered = { commands: {}, completion: null, hover: null, formatting: null, events: {}, output: [], status: [] };
    const collections = [];

    class Position {
        constructor(line, character) { this.line = line; this.character = character; }
    }
    class Range {
        constructor(a, b, c, d) {
            if (a instanceof Position) { this.start = a; this.end = b; }
            else { this.start = new Position(a, b); this.end = new Position(c, d); }
        }
    }
    class SnippetString { constructor(value) { this.value = value; } }
    class MarkdownString {
        constructor() { this.value = ""; }
        appendMarkdown(text) { this.value += text; return this; }
        appendCodeblock(text) { this.value += text; return this; }
    }
    class CompletionItem { constructor(label, kind) { this.label = label; this.kind = kind; } }
    class Diagnostic {
        constructor(range, message, severity) { this.range = range; this.message = message; this.severity = severity; }
    }
    const event = (name) => (handler) => { registered.events[name] = handler; return { dispose() {} }; };
    const configuration = { compilerPath: compiler, "diagnostics.enable": true, "devServer.port": 3000, ...overrides };

    const vscode = {
        Position, Range, SnippetString, MarkdownString, CompletionItem, Diagnostic,
        Hover: class { constructor(contents, range) { this.contents = contents; this.range = range; } },
        TextEdit: { replace: (range, newText) => ({ range, newText }) },
        Uri: { parse: (value) => ({ value }) },
        CompletionItemKind: { Keyword: 1, Property: 2, EnumMember: 3, Value: 4 },
        DiagnosticSeverity: { Error: 0, Warning: 1 },
        window: {
            createOutputChannel: () => ({
                appendLine: (line) => registered.output.push(line),
                append: (text) => registered.output.push(text),
                show() {},
                dispose() {},
            }),
            showWarningMessage: () => Promise.resolve(undefined),
            showErrorMessage: () => Promise.resolve(undefined),
            showInformationMessage: () => Promise.resolve(undefined),
            setStatusBarMessage: (message) => registered.status.push(message),
            activeTextEditor: undefined,
        },
        workspace: {
            isTrusted: true,
            textDocuments: [],
            workspaceFolders: [],
            getWorkspaceFolder: () => undefined,
            getConfiguration: () => ({ get: (key, fallback) => (key in configuration ? configuration[key] : fallback) }),
            onDidOpenTextDocument: event("open"),
            onDidChangeTextDocument: event("change"),
            onDidSaveTextDocument: event("save"),
            onDidCloseTextDocument: event("close"),
            onDidChangeConfiguration: event("configuration"),
        },
        languages: {
            createDiagnosticCollection: () => {
                const store = new Map();
                const collection = {
                    set: (uri, items) => store.set(uri.toString(), items),
                    get: (uri) => store.get(uri.toString()),
                    delete: (uri) => store.delete(uri.toString()),
                    dispose() {},
                };
                collections.push(collection);
                return collection;
            },
            registerCompletionItemProvider: (selector, provider) => { registered.completion = provider; return { dispose() {} }; },
            registerHoverProvider: (selector, provider) => { registered.hover = provider; return { dispose() {} }; },
            registerDocumentFormattingEditProvider: (selector, provider) => { registered.formatting = provider; return { dispose() {} }; },
        },
        commands: {
            registerCommand: (name, handler) => { registered.commands[name] = handler; return { dispose() {} }; },
            executeCommand: () => Promise.resolve(),
        },
        env: { openExternal: () => Promise.resolve(true) },
    };

    return { vscode, registered, collections };
}

function createDocument(text) {
    const lines = text.split("\n");
    return {
        languageId: "genesis",
        version: 1,
        isClosed: false,
        isDirty: false,
        uri: { scheme: "untitled", toString: () => "untitled:test.gns", fsPath: "test.gns" },
        getText(range) {
            if (!range) {
                return text;
            }
            const offset = (p) => lines.slice(0, p.line).reduce((n, l) => n + l.length + 1, 0) + p.character;
            return text.slice(offset(range.start), offset(range.end));
        },
        get lineCount() { return lines.length; },
        lineAt(index) { return { text: lines[index], lineNumber: index }; },
        getWordRangeAtPosition() { return undefined; },
    };
}

function loadExtension(mock) {
    const originalResolve = Module._resolveFilename;
    const originalLoad = Module._load;
    Module._load = function (request, parent, isMain) {
        if (request === "vscode") {
            return mock.vscode;
        }
        return originalLoad.call(this, request, parent, isMain);
    };
    try {
        const file = path.join(__dirname, "..", "extension.js");
        delete require.cache[require.resolve(file)];
        return require(file);
    } finally {
        Module._load = originalLoad;
        Module._resolveFilename = originalResolve;
    }
}

const skip = { skip: compiler ? false : "compiler not built (run make, or set GENESIS_BIN)" };

test("activation registers providers and commands", skip, () => {
    const mock = createMock();
    const extension = loadExtension(mock);
    extension.activate({ subscriptions: [] });

    assert.ok(mock.registered.completion);
    assert.ok(mock.registered.hover);
    assert.ok(mock.registered.formatting);
    for (const command of ["genesis.build", "genesis.startDevServer", "genesis.stopDevServer"]) {
        assert.equal(typeof mock.registered.commands[command], "function", command);
    }
    extension.deactivate();
});

test("diagnostics come from the compiler with correct ranges", skip, async () => {
    const mock = createMock();
    const extension = loadExtension(mock);
    extension.activate({ subscriptions: [] });

    const document = createDocument('page A {\n    text "\u{1F680}" size = 3\n}\n');
    mock.registered.events.open(document);
    await new Promise((resolve) => setTimeout(resolve, 1500));

    const items = mock.collections[0].get(document.uri);
    assert.ok(items, "diagnostics were published");
    assert.equal(items.length, 1);
    assert.match(items[0].message, /Invalid size 3/);
    /* The 3 is at code point column 21. Twenty code points precede it, one of
       them the emoji (two UTF-16 units), so the UTF-16 offset is 21. */
    assert.equal(items[0].range.start.line, 1);
    assert.equal(items[0].range.start.character, 21);
    extension.deactivate();
});

test("completion suggests elements and element-specific properties", skip, () => {
    const mock = createMock();
    const extension = loadExtension(mock);
    extension.activate({ subscriptions: [] });

    const statementDoc = createDocument("page A {\n    ");
    const statementItems = mock.registered.completion
        .provideCompletionItems(statementDoc, new mock.vscode.Position(1, 4))
        .map((i) => i.label);
    assert.deepEqual(statementItems.sort(), ["button", "column", "heading", "image", "link", "row", "text"]);

    const propertyDoc = createDocument('page A {\n    link "x" to = "a.html" ');
    const labels = mock.registered.completion
        .provideCompletionItems(propertyDoc, new mock.vscode.Position(1, 27))
        .map((i) => i.label);
    assert.ok(labels.includes("size"));
    assert.ok(labels.includes("color"));
    assert.ok(!labels.includes("to"), "already used");
    assert.ok(!labels.includes("gap"), "not valid on link");
    extension.deactivate();
});

test("formatting uses genesis fmt", skip, async () => {
    const mock = createMock();
    const extension = loadExtension(mock);
    extension.activate({ subscriptions: [] });

    const edits = await mock.registered.formatting.provideDocumentFormattingEdits(
        createDocument('page A{text "x"}'));
    assert.equal(edits.length, 1);
    assert.equal(edits[0].newText, 'page A {\n    text "x"\n}\n');

    const none = await mock.registered.formatting.provideDocumentFormattingEdits(
        createDocument('page A{text "x"'));
    assert.deepEqual(none, [], "invalid documents are not formatted");
    extension.deactivate();
});

test("a compiler that fails without diagnostics is reported, not shown as a clean file", skip, async () => {
    /* Node runs, but it is not Genesis+: `node check --stdin ...` exits with
       status 1 and no machine-readable diagnostics. */
    const mock = createMock({ compilerPath: process.execPath });
    const extension = loadExtension(mock);
    extension.activate({ subscriptions: [] });

    const document = createDocument('page A { text "x" }\n');
    mock.registered.events.open(document);
    await new Promise((resolve) => setTimeout(resolve, 1500));

    assert.equal(mock.collections[0].get(document.uri), undefined);
    assert.ok(mock.registered.output.some((line) => /failed with exit status 1/.test(line)),
        mock.registered.output.join("\n"));
    assert.ok(mock.registered.status.some((message) => /compiler failed/.test(message)));
    extension.deactivate();
});
