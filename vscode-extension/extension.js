"use strict";

/*
 * Genesis+ for Visual Studio Code.
 *
 * Diagnostics and formatting come from the real Genesis+ compiler
 * (`genesis check --stdin --machine` and `genesis fmt --stdin`), so the
 * editor always agrees with `genesis build`. Completions, hovers and
 * snippets use the language table in lib/language.js.
 */

const vscode = require("vscode");
const childProcess = require("child_process");
const fs = require("fs");
const path = require("path");

const { ELEMENTS, PROPERTIES, PADDING_SIDES, ALIGN_VALUES, CHILD_ELEMENTS } = require("./lib/language");
const {
    parseMachineDiagnostics,
    codePointColumnToOffset,
    codePointLengthToUtf16,
    completionContext,
} = require("./lib/analysis");

const LANGUAGE_ID = "genesis";
const CHECK_DELAY_MS = 250;
const PROCESS_TIMEOUT_MS = 10000;

let output;
let diagnostics;
let devServer = null;
let warnedMissingCompiler = false;
const pendingChecks = new Map();

/* ------------------------------------------------------------------------ */
/* Locating and running the compiler                                         */
/* ------------------------------------------------------------------------ */

function compilerCandidates(document) {
    const configured = vscode.workspace.getConfiguration("genesis").get("compilerPath");
    if (configured && configured.trim() !== "") {
        return [configured.trim()];
    }

    const names = process.platform === "win32" ? ["genesis.exe"] : ["genesis"];
    const candidates = [];
    const folder = document ? vscode.workspace.getWorkspaceFolder(document.uri) : undefined;
    /* Never run a program shipped inside an untrusted folder. */
    const folders = !vscode.workspace.isTrusted
        ? []
        : folder ? [folder] : vscode.workspace.workspaceFolders || [];

    for (const f of folders) {
        for (const name of names) {
            const candidate = path.join(f.uri.fsPath, name);
            if (fs.existsSync(candidate)) {
                candidates.push(candidate);
            }
        }
    }

    candidates.push("genesis"); /* on PATH */
    return candidates;
}

function resolveCompiler(document) {
    return compilerCandidates(document)[0];
}

/**
 * Runs the compiler with an argument array (never through a shell) and
 * optional standard input. Resolves with { code, stdout, stderr } or rejects
 * with an Error whose `code` is "ENOENT" when the compiler is missing.
 */
function runCompiler(document, args, input, cwd) {
    const compiler = resolveCompiler(document);

    return new Promise((resolve, reject) => {
        const child = childProcess.execFile(
            compiler,
            args,
            { cwd, timeout: PROCESS_TIMEOUT_MS, maxBuffer: 16 * 1024 * 1024, windowsHide: true },
            (error, stdout, stderr) => {
                if (error && (error.code === "ENOENT" || error.code === "EACCES")) {
                    reject(error);
                    return;
                }
                if (error && error.killed) {
                    reject(new Error("The Genesis+ compiler took too long and was stopped."));
                    return;
                }
                const code = error && typeof error.code === "number" ? error.code : 0;
                resolve({ code, stdout: String(stdout), stderr: String(stderr) });
            }
        );

        if (input !== undefined && child.stdin) {
            child.stdin.on("error", () => {});
            child.stdin.end(input, "utf8");
        }
    });
}

function reportMissingCompiler() {
    if (warnedMissingCompiler) {
        return;
    }
    warnedMissingCompiler = true;
    vscode.window
        .showWarningMessage(
            "Genesis+: the compiler was not found, so errors cannot be checked. " +
                "Build it (see the README) and set 'genesis.compilerPath', or add it to your PATH.",
            "Open Settings"
        )
        .then((choice) => {
            if (choice === "Open Settings") {
                vscode.commands.executeCommand("workbench.action.openSettings", "genesis.compilerPath");
            }
        });
}

/* ------------------------------------------------------------------------ */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------ */

async function checkDocument(document) {
    if (document.languageId !== LANGUAGE_ID) {
        return;
    }
    if (!vscode.workspace.getConfiguration("genesis").get("diagnostics.enable", true)) {
        diagnostics.delete(document.uri);
        return;
    }

    const version = document.version;
    const args = ["check", "--stdin", "--machine"];
    let cwd;

    if (document.uri.scheme === "file") {
        args.push("--stdin-filename", document.uri.fsPath);
        cwd = path.dirname(document.uri.fsPath);
    }

    let result;
    try {
        result = await runCompiler(document, args, document.getText(), cwd);
    } catch (error) {
        if (error.code === "ENOENT" || error.code === "EACCES") {
            reportMissingCompiler();
        } else {
            output.appendLine(String(error.message || error));
        }
        diagnostics.delete(document.uri);
        return;
    }

    /* Ignore results for an outdated version of the document. */
    if (document.isClosed || document.version !== version) {
        return;
    }

    const parsed = parseMachineDiagnostics(result.stdout);

    /* Exit status 1 with diagnostics is a normal "the file has errors".
       Anything else without diagnostics means the compiler itself failed
       (crash, wrong binary, too old for --machine): say so instead of
       silently showing a clean file. */
    if (result.code !== 0 && parsed.length === 0) {
        const detail = (result.stderr || result.stdout).trim().split(/\r?\n/)[0] || "no output";
        output.appendLine(`Genesis+: '${resolveCompiler(document)}' failed with exit status ${result.code}: ${detail}`);
        vscode.window.setStatusBarMessage("Genesis+: the compiler failed; see the Genesis+ output.", 6000);
        diagnostics.delete(document.uri);
        return;
    }

    const items = parsed.map((d) => {
        const lineIndex = Math.min(Math.max(d.line - 1, 0), Math.max(document.lineCount - 1, 0));
        const lineText = document.lineAt(lineIndex).text;
        const start = codePointColumnToOffset(lineText, d.column);
        const length = codePointLengthToUtf16(lineText, start, d.length);
        const range = new vscode.Range(lineIndex, start, lineIndex, start + length);
        const diagnostic = new vscode.Diagnostic(
            range,
            d.message,
            d.severity === "warning" ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error
        );
        diagnostic.source = "Genesis+";
        return diagnostic;
    });

    diagnostics.set(document.uri, items);
}

function scheduleCheck(document) {
    if (document.languageId !== LANGUAGE_ID) {
        return;
    }
    const key = document.uri.toString();
    clearTimeout(pendingChecks.get(key));
    pendingChecks.set(
        key,
        setTimeout(() => {
            pendingChecks.delete(key);
            checkDocument(document);
        }, CHECK_DELAY_MS)
    );
}

/* ------------------------------------------------------------------------ */
/* Formatting                                                                */
/* ------------------------------------------------------------------------ */

async function formatDocument(document) {
    let result;
    try {
        result = await runCompiler(document, ["fmt", "--stdin"], document.getText(), undefined);
    } catch (error) {
        if (error.code === "ENOENT" || error.code === "EACCES") {
            reportMissingCompiler();
        }
        return [];
    }

    if (result.code !== 0) {
        vscode.window.setStatusBarMessage("Genesis+: fix the errors before formatting.", 4000);
        return [];
    }

    if (result.stdout === document.getText()) {
        return [];
    }

    const lastLine = document.lineAt(document.lineCount - 1);
    const fullRange = new vscode.Range(0, 0, lastLine.lineNumber, lastLine.text.length);
    return [vscode.TextEdit.replace(fullRange, result.stdout)];
}

/* ------------------------------------------------------------------------ */
/* Completion and hover                                                      */
/* ------------------------------------------------------------------------ */

function elementItem(name, sortPrefix) {
    const spec = ELEMENTS[name];
    const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Keyword);
    item.detail = spec.description;
    item.documentation = new vscode.MarkdownString().appendCodeblock(spec.example, LANGUAGE_ID);
    item.sortText = sortPrefix + name;

    if (spec.block && name !== "page") {
        item.insertText = new vscode.SnippetString(`${name} {\n\t$0\n}`);
    } else if (name === "page") {
        item.insertText = new vscode.SnippetString("page ${1:Home} {\n\t$0\n}");
    } else if (name === "link" || name === "button") {
        item.insertText = new vscode.SnippetString(`${name} "\${1:Text}" to = "\${2:about.html}"`);
    } else if (name === "image") {
        item.insertText = new vscode.SnippetString('image "assets/${1:photo.jpg}" alt = "${2:Description}"');
    } else {
        item.insertText = new vscode.SnippetString(`${name} "\${1}"`);
    }
    return item;
}

function propertyItem(name) {
    const spec = PROPERTIES[name];
    const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Property);
    item.detail = spec.description;
    item.sortText = "0" + name;

    if (name === "padding") {
        item.insertText = new vscode.SnippetString("padding = ${1:24}");
    } else if (spec.kind === "number") {
        item.insertText = new vscode.SnippetString(`${name} = \${1:${spec.example}}`);
    } else if (spec.kind === "percent") {
        item.insertText = new vscode.SnippetString(`${name} = \${1:50}%`);
    } else if (spec.kind === "align") {
        item.insertText = new vscode.SnippetString(`${name} = "\${1|left,center,right|}"`);
    } else if (spec.kind === "color") {
        item.insertText = new vscode.SnippetString(`${name} = "#\${1:7c3aed}"`);
    } else {
        item.insertText = new vscode.SnippetString(`${name} = "\${1}"`);
    }
    return item;
}

function provideCompletionItems(document, position) {
    const textBefore = document.getText(new vscode.Range(new vscode.Position(0, 0), position));
    const context = completionContext(textBefore);
    const items = [];

    switch (context.kind) {
    case "statement":
        if (context.topLevel) {
            items.push(elementItem("page", "0"));
        } else {
            for (const name of CHILD_ELEMENTS) {
                items.push(elementItem(name, "1"));
            }
        }
        break;

    case "properties": {
        for (const name of ELEMENTS[context.element].properties) {
            if (name === "padding") {
                if (!context.used.includes("padding")) {
                    items.push(propertyItem("padding"));
                }
                for (const side of PADDING_SIDES) {
                    if (!context.used.includes("padding " + side)) {
                        const item = new vscode.CompletionItem(`padding ${side}`, vscode.CompletionItemKind.Property);
                        item.detail = `Padding on the ${side} side(s) only, in pixels.`;
                        item.insertText = new vscode.SnippetString(`padding ${side} = \${1:16}`);
                        item.sortText = "0padding " + side;
                        items.push(item);
                    }
                }
            } else if (!context.used.includes(name)) {
                items.push(propertyItem(name));
            }
        }
        if (context.alsoStatement) {
            for (const name of CHILD_ELEMENTS) {
                items.push(elementItem(name, "1"));
            }
        }
        break;
    }

    case "paddingSide":
        for (const side of PADDING_SIDES) {
            if (!context.used.includes("padding " + side)) {
                const item = new vscode.CompletionItem(side, vscode.CompletionItemKind.EnumMember);
                item.insertText = new vscode.SnippetString(`${side} = \${1:16}`);
                items.push(item);
            }
        }
        break;

    case "value":
        if (context.property === "align") {
            for (const value of ALIGN_VALUES) {
                const item = new vscode.CompletionItem(`"${value}"`, vscode.CompletionItemKind.Value);
                items.push(item);
            }
        }
        break;

    default:
        break;
    }

    return items;
}

function provideHover(document, position) {
    const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
    if (!range) {
        return undefined;
    }

    /* No hovers inside strings or comments. */
    const before = document.getText(new vscode.Range(new vscode.Position(0, 0), range.start));
    if (completionContext(before).kind === "none") {
        return undefined;
    }

    const word = document.getText(range);
    const markdown = new vscode.MarkdownString();

    if (Object.prototype.hasOwnProperty.call(ELEMENTS, word)) {
        const spec = ELEMENTS[word];
        markdown.appendMarkdown(`**${word}** — ${spec.description}\n\n`);
        markdown.appendCodeblock(spec.example, LANGUAGE_ID);
        markdown.appendMarkdown(`\nProperties: ${spec.properties.map((p) => "`" + p + "`").join(", ")}`);
        return new vscode.Hover(markdown, range);
    }

    if (Object.prototype.hasOwnProperty.call(PROPERTIES, word)) {
        const spec = PROPERTIES[word];
        const usedBy = Object.keys(ELEMENTS).filter((e) => ELEMENTS[e].properties.includes(word));
        markdown.appendMarkdown(`**${word}** — ${spec.description}\n\n`);
        markdown.appendCodeblock(`${word} = ${spec.example}`, LANGUAGE_ID);
        markdown.appendMarkdown(`\nUsed on: ${usedBy.join(", ")}`);
        return new vscode.Hover(markdown, range);
    }

    return undefined;
}

/* ------------------------------------------------------------------------ */
/* Commands                                                                  */
/* ------------------------------------------------------------------------ */

async function activeGenesisDocument() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== LANGUAGE_ID || editor.document.uri.scheme !== "file") {
        vscode.window.showErrorMessage("Genesis+: open a saved .gns file first.");
        return undefined;
    }
    if (editor.document.isDirty) {
        await editor.document.save();
    }
    return editor.document;
}

async function buildCommand() {
    const document = await activeGenesisDocument();
    if (!document) {
        return;
    }

    output.show(true);
    output.appendLine(`> genesis build ${document.uri.fsPath}`);

    try {
        const result = await runCompiler(document, ["build", document.uri.fsPath], undefined,
            path.dirname(document.uri.fsPath));
        output.append(result.stdout);
        output.append(result.stderr);
        if (result.code === 0) {
            vscode.window.setStatusBarMessage("Genesis+: website built into dist/", 4000);
        } else {
            vscode.window.showErrorMessage("Genesis+: the build failed. See the Genesis+ output for details.");
        }
    } catch (error) {
        if (error.code === "ENOENT" || error.code === "EACCES") {
            warnedMissingCompiler = false;
            reportMissingCompiler();
        } else {
            output.appendLine(String(error.message || error));
        }
    }
}

async function startDevServer() {
    if (devServer) {
        vscode.window.showInformationMessage("Genesis+: the dev server is already running.");
        return;
    }

    const document = await activeGenesisDocument();
    if (!document) {
        return;
    }

    const port = vscode.workspace.getConfiguration("genesis").get("devServer.port", 3000);
    const compiler = resolveCompiler(document);
    const args = ["dev", document.uri.fsPath, "--port", String(port)];

    output.show(true);
    output.appendLine(`> genesis ${args.join(" ")}`);

    const child = childProcess.spawn(compiler, args, {
        cwd: path.dirname(document.uri.fsPath),
        windowsHide: true,
    });
    devServer = child;

    child.stdout.on("data", (data) => output.append(String(data)));
    child.stderr.on("data", (data) => output.append(String(data)));
    child.on("error", (error) => {
        if (error.code === "ENOENT" || error.code === "EACCES") {
            warnedMissingCompiler = false;
            reportMissingCompiler();
        } else {
            output.appendLine(String(error.message || error));
        }
        if (devServer === child) {
            devServer = null;
        }
    });
    child.on("exit", (code) => {
        output.appendLine(`Genesis+ dev server exited (${code === null ? "stopped" : "code " + code}).`);
        if (devServer === child) {
            devServer = null;
        }
    });

    const url = `http://localhost:${port}/`;
    const choice = await vscode.window.showInformationMessage(
        `Genesis+ dev server starting at ${url}`, "Open in Browser");
    if (choice === "Open in Browser") {
        vscode.env.openExternal(vscode.Uri.parse(url));
    }
}

function stopDevServer() {
    if (!devServer) {
        vscode.window.showInformationMessage("Genesis+: the dev server is not running.");
        return;
    }
    devServer.kill();
    devServer = null;
}

/* ------------------------------------------------------------------------ */
/* Activation                                                                */
/* ------------------------------------------------------------------------ */

function activate(context) {
    output = vscode.window.createOutputChannel("Genesis+");
    diagnostics = vscode.languages.createDiagnosticCollection(LANGUAGE_ID);
    context.subscriptions.push(output, diagnostics);

    for (const document of vscode.workspace.textDocuments) {
        scheduleCheck(document);
    }

    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(scheduleCheck),
        vscode.workspace.onDidChangeTextDocument((event) => scheduleCheck(event.document)),
        vscode.workspace.onDidSaveTextDocument(scheduleCheck),
        vscode.workspace.onDidCloseTextDocument((document) => diagnostics.delete(document.uri)),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration("genesis")) {
                warnedMissingCompiler = false;
                for (const document of vscode.workspace.textDocuments) {
                    scheduleCheck(document);
                }
            }
        }),
        vscode.languages.registerCompletionItemProvider(
            { language: LANGUAGE_ID },
            { provideCompletionItems },
            " ", "="
        ),
        vscode.languages.registerHoverProvider({ language: LANGUAGE_ID }, { provideHover }),
        vscode.languages.registerDocumentFormattingEditProvider(
            { language: LANGUAGE_ID },
            { provideDocumentFormattingEdits: formatDocument }
        ),
        vscode.commands.registerCommand("genesis.build", buildCommand),
        vscode.commands.registerCommand("genesis.startDevServer", startDevServer),
        vscode.commands.registerCommand("genesis.stopDevServer", stopDevServer)
    );
}

function deactivate() {
    if (devServer) {
        devServer.kill();
        devServer = null;
    }
}

module.exports = { activate, deactivate };
