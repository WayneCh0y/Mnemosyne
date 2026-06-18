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

> **Not yet implemented.** `.tex` files are recognised by extension but ingestion will fail with an error in v1. Support is planned for a future version.

---

## `.pdf` — PDF

> **Not yet implemented.** `.pdf` files are recognised by extension but ingestion will fail with an error in v1. Support is planned for a future version.

---

## Adding New File Types (for contributors)

1. Create `src/parser/<ext>.c` and `src/parser/<ext>.h`.
2. Implement `char *parse_<ext>(const char *path)` — returns heap-allocated plain text; caller frees.
3. Register the extension in `ingest.c` in the `route_parser()` function.
4. Add a test in `tests/test_ingest.c`.
