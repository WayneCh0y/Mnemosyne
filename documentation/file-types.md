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

Read as plain text. Markdown syntax markers are stripped so that search results show readable prose rather than raw markup.

**Stripped patterns**

| Pattern | Example input | Stored as |
|---|---|---|
| ATX headings | `## Section Title` | `Section Title` |
| Bold / italic | `**word**`, `*word*` | `word` |
| Inline code | `` `code` `` | `code` |
| Links | `[text](url)` | `text` |
| Images | `![alt](url)` | `alt` |
| Blockquote markers | `> quote` | `quote` |
| Horizontal rules | `---` | _(removed)_ |

Fenced code blocks (` ```...``` `) are preserved as plain text since they often contain searchable content.

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
