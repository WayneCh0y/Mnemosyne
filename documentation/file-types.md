# Supported File Types

Mnemosyne extracts plain text from each ingested file and stores it in the index. The extraction strategy depends on the file format.

---

## `.txt` — Plain Text

**Parser:** `src/parser/txt.c`

Read directly with `fread`. No transformation applied. The raw byte content is stored as-is.

**Edge cases**
- Files with non-UTF-8 encodings are stored as-is; search results may look garbled for non-ASCII content in v1.
- Very large files (>50 MB) may be truncated in v1 with a warning.

---

## `.md` — Markdown

**Parser:** `src/parser/md.c`

Markdown is converted to a structured plain-text format. All output is lowercased. Inline formatting delimiters are stripped; block structure is captured with lightweight tokens used by the search display layer.

**Transformation table**

| Input | Stored as |
|---|---|
| `# Heading` / `## Heading` | `# heading` / `## heading` (hash markers preserved) |
| `**bold**`, `*italic*`, `__bold__`, `_italic_` | plain text (delimiters stripped) |
| `` `inline code` `` | content kept |
| ` ```fenced block``` ` | content kept (lowercased) |
| `[text](url)` links | `[LINK]` token |
| `![alt](url)` images | _(removed entirely)_ |
| `> blockquote` | content only, `>` stripped |
| `---` horizontal rules | _(removed entirely)_ |
| `$...$`, `$$...$$` math | content kept, `$` delimiters stripped |
| List items (`-`, `*`, `1.`) | single line: `[LIST] item one \| item two [/LIST]` |

**Search display rendering**

The tokens in the stored doc are interpreted by `print_context` in `command_handler.c` when showing results:

| Token | Rendered as |
|---|---|
| `# heading text` | heading text in magenta |
| `[LIST] … [/LIST]` | `- item one` / `- item two` (one bullet per line) |
| `[LINK]` | `link` in blue |

---

## `.tex` — LaTeX

**Parser:** `src/parser/tex.c`

LaTeX source is stripped of markup so that the underlying prose and math is searchable.

**Stripped patterns**

| Pattern | Example input | Stored as |
|---|---|---|
| Commands with args | `\textbf{word}` | `word` |
| Commands no args | `\newpage` | _(removed)_ |
| Comments | `% this is a comment` | _(removed)_ |
| Environment tags | `\begin{equation}` | _(removed)_ |
| Math delimiters | `$x^2$`, `\[...\]` | kept as-is (v1) |

The goal is to make prose sections (abstract, introduction, body) fully searchable. Math expressions are retained verbatim in v1.

---

## `.pdf` — PDF

**Parser:** `src/parser/pdf.c`

**v1 strategy:** shell out to `pdftotext` (part of the [Poppler](https://poppler.freedesktop.org/) utilities):

```c
snprintf(cmd, sizeof(cmd), "pdftotext \"%s\" -", path);
FILE *pipe = popen(cmd, "r");
// read stdout into buffer
```

This requires `pdftotext` to be installed and on `PATH`.

**Installation**

| Platform | Command |
|---|---|
| Ubuntu/Debian | `sudo apt install poppler-utils` |
| macOS (Homebrew) | `brew install poppler` |
| Windows (via scoop) | `scoop install poppler` |

**Limitations (v1)**
- Scanned PDFs (image-only, no embedded text layer) will produce empty output. OCR support is a v2+ feature.
- PDFs with complex column layouts may have text extracted in the wrong reading order.
- Password-protected PDFs will fail silently; a warning is printed.

---

## Adding New File Types (for contributors)

1. Create `src/parser/<ext>.c` and `src/parser/<ext>.h`.
2. Implement `char *parse_<ext>(const char *path)` — returns heap-allocated plain text; caller frees.
3. Register the extension in `ingest.c` in the `route_parser()` function.
4. Add a test in `tests/test_ingest.c`.
