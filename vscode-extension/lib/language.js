"use strict";

/*
 * The Genesis+ language surface, as understood by the editor.
 *
 * This table must match the compiler (src/parser.c). The extension tests
 * (test/language.test.js) compile every combination listed here with the
 * real compiler, so a mismatch fails the test suite.
 */

const ELEMENTS = {
    page: {
        description: "A web page. The first page becomes index.html; others become <name>.html.",
        example: 'page Home {\n    heading "Welcome"\n}',
        takesText: false,
        block: true,
        properties: ["title", "background", "color"],
    },
    column: {
        description: "Stacks its children vertically.",
        example: "column gap = 16 {\n    ...\n}",
        takesText: false,
        block: true,
        properties: ["background", "gap", "width", "padding", "align"],
    },
    row: {
        description: "Places its children side by side. Stacks vertically on narrow screens.",
        example: "row gap = 16 {\n    ...\n}",
        takesText: false,
        block: true,
        properties: ["background", "gap", "width", "padding", "align"],
    },
    heading: {
        description: "A heading (h1-h6).",
        example: 'heading "Welcome" level = 2',
        takesText: true,
        block: false,
        properties: ["size", "color", "level", "align"],
    },
    text: {
        description: "A paragraph of text.",
        example: 'text "Hello there"',
        takesText: true,
        block: false,
        properties: ["size", "color", "align"],
    },
    link: {
        description: "A text link to another page or address.",
        example: 'link "About us" to = "about.html"',
        takesText: true,
        block: false,
        properties: ["to", "size", "color"],
    },
    button: {
        description: "A link styled as a button.",
        example: 'button "Get started" to = "#start"',
        takesText: true,
        block: false,
        properties: ["to", "size", "color", "background"],
    },
    image: {
        description: "An image from the assets folder or a web address.",
        example: 'image "assets/photo.jpg" alt = "A photo"',
        takesText: true,
        block: false,
        properties: ["alt", "width"],
    },
};

const PROPERTIES = {
    title: { kind: "string", description: "The page title shown in the browser tab.", example: '"My website"' },
    background: { kind: "color", description: "Background color as #RGB or #RRGGBB.", example: '"#f5f3ff"' },
    color: { kind: "color", description: "Text color as #RGB or #RRGGBB.", example: '"#7c3aed"' },
    size: { kind: "number", min: 8, max: 1000, description: "Font size in pixels (8-1000).", example: "24" },
    level: { kind: "number", min: 1, max: 6, description: "Heading level 1-6 (default 1).", example: "2" },
    align: { kind: "align", description: 'Alignment: "left", "center" or "right".', example: '"center"' },
    gap: { kind: "number", min: 0, max: 256, description: "Space between children in pixels (0-256).", example: "16" },
    width: { kind: "percent", description: "Width as a percentage of the available space (more than 0%, at most 100%).", example: "50%" },
    padding: { kind: "number", min: 0, max: 256, description: "Space inside the block in pixels (0-256). Optionally followed by a side: top, bottom, left, right, inline, block.", example: "24" },
    to: { kind: "url", description: 'Destination: a page ("about.html"), a section ("#contact"), or an address starting with https://, http://, mailto: or tel:.', example: '"about.html"' },
    alt: { kind: "string", description: "A short description of the image for screen readers.", example: '"A photo of the team"' },
};

const PADDING_SIDES = ["top", "bottom", "left", "right", "inline", "block"];
const ALIGN_VALUES = ["left", "center", "right"];

/* Elements that may appear inside a page, column or row. */
const CHILD_ELEMENTS = ["column", "row", "heading", "text", "link", "button", "image"];

module.exports = { ELEMENTS, PROPERTIES, PADDING_SIDES, ALIGN_VALUES, CHILD_ELEMENTS };
