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

Markdown is converted to a structured plain-text format. Inline formatting delimiters are stripped; block structure is captured with lightweight tokens used by the search display layer. Original casing is preserved so `mn search -c` works correctly against `.md` files.

**Transformation table**

| Input | Stored as |
|---|---|
| `# Heading` / `## Heading` | `# Heading` / `## Heading` (hash markers preserved) |
| `**bold**`, `*italic*`, `__bold__`, `_italic_` | plain text (delimiters stripped) |
| `` `inline code` `` | content kept |
| ` ```fenced block``` ` | content kept |
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

> **Not yet implemented.** `.tex` files are recognised by extension but ingestion will fail with an error in v1. Support is planned for a future version.

---

## `.pdf` — PDF

**Parser:** `src/parser/pdf.c`

PDF support is **optional** and requires the `pdftotext` binary from poppler-utils. Without it, `mn add` on a `.pdf` prints an install hint and skips the file — all other file types continue to work. See [Enabling PDF support](../README.md#enabling-pdf-support) in the README for per-OS setup.

**Strategy.** `parse_pdf` shells out to `pdftotext` via `posix_spawnp` (POSIX) or `_spawnvp` (Windows), writes plain text to a temp file (`$TEMP/mn_pdf_<time>_<pid>.txt` on Windows, `/tmp/mn_pdf_*` on POSIX), reads it back, and deletes the temp file. Original casing is preserved. Flags: `-raw -enc UTF-8` — raw stream order (no column reconstruction), UTF-8 output. Page breaks are kept as inline form-feed bytes (`\f`, 0x0C) between pages; the search layer counts them to report the page number of a match, and `mn search` uses that to jump the PDF viewer to the matching page on open (see [`mn search`](commands.md#mn-search-query) for the viewer-support matrix).

**Why shell out instead of linking a library?** Text-extraction quality is roughly equivalent to libmupdf for search indexing, and a runtime binary keeps the build simple — no vendored ~120 MB MuPDF source tree, no per-platform link wiring. The tradeoff is one external dependency on macOS/Linux and one bundled folder on Windows.

**Resolution order (Windows).** `pdftotext.exe` is located by `find_pdftotext` in this order:
1. Next to `mn.exe` — `<dir>\pdftotext.exe`
2. `poppler\bin\` next to `mn.exe` — `<dir>\poppler\bin\pdftotext.exe` (the layout produced by `make fetch-poppler` followed by `make install`)
3. `PATH`

On macOS/Linux only `PATH` is checked.

**Edge cases**
- **Filenames with spaces work.** The Windows branch wraps `input` and `output` in double quotes before passing them to `_spawnvp`. This is required because the Microsoft CRT joins argv into a single command-line string without auto-quoting whitespace-containing elements; without it, `"My Notes.pdf"` would be re-split by the child into `My` and `Notes.pdf`.
- **Scanned PDFs without an embedded text layer** produce empty or near-empty output. OCR (e.g. via Tesseract) is listed under future ideas in [roadmap.md](roadmap.md).
- **Encrypted PDFs that require a password** fail with a non-zero exit from `pdftotext`; no password prompt is exposed.

---

## Adding New File Types (for contributors)

1. Create `src/parser/<ext>.c` and `src/parser/<ext>.h`.
2. Implement `char *parse_<ext>(const char *path)` — returns heap-allocated plain text; caller frees.
3. Register the extension in `ingest.c` in the `route_parser()` function.
4. Add a test in `tests/test_ingest.c`.
