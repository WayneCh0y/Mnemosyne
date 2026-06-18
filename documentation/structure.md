# System Architecture

## Overview

Mnemosyne is a local-first, command-line file search tool. It ingests plain and document files into a local index, then lets you search across all of them or browse the full list, and open results directly in your preferred IDE.

---

## Component Diagram

```mermaid
graph TD
    User([User])

    subgraph CLI ["mnemosyne CLI"]
        CMD[Command Handler\ncommand_handler.c]
    end

    subgraph Ingestion ["Ingestion Pipeline"]
        ING[Ingestor\ningest.c]
        P_TXT[Plain Text Parser\nparser/txt.c]
        P_MD[Markdown Parser\nparser/md.c]
    end

    subgraph Storage ["Index Store (~/.mnemosyne/)"]
        MANIFEST[manifest.json\nfile registry]
        DOCS[docs/\nhashed plain-text copies]
        CONF[~/.mnemosyne.conf\nuser settings]
    end

    subgraph Search ["Search Engine"]
        SE[Keyword Matcher\nsearch.c]
    end

    User -- "mnemosyne add file" --> CMD
    CMD --> ING
    ING -- ".txt" --> P_TXT
    ING -- ".md" --> P_MD
    P_TXT & P_MD --> MANIFEST
    P_TXT & P_MD --> DOCS

    User -- "mnemosyne search query" --> CMD
    CMD --> SE
    SE -- reads --> DOCS
    SE -- reads --> MANIFEST
    CMD -- "user picks result" --> CMD
    CMD -- opens file --> User

    User -- "mnemosyne list" --> CMD
    CMD -- reads --> MANIFEST
    CMD -- "user picks entry" --> CMD
    CMD -- opens file --> User

    User -- "mnemosyne config ide name" --> CMD
    CMD --> CONF
```

---

## Component Descriptions

### `main.c` — Entry Point
Runs first-time setup if needed, loads config, then delegates to `handle_command()` in `command_handler.c`.

### `command_handler.c` — Command Dispatcher
Routes `argv[1]` to the correct handler and implements all interactive UI logic:

| Subcommand | Handler |
|---|---|
| `add` | `ingest_file()` |
| `search` | `cmd_search()` → `run_picker()` → `handle_enter()` |
| `list` | `cmd_list()` → `run_list_picker()` → `handle_list_enter()` |
| `config` | `cmd_config()` → `set_ide()` |
| `remove` | `cmd_remove()` → `remove_file()` |
| `help` | `print_help()` |

Also contains the interactive terminal picker (ANSI rendering, arrow-key input, IDE launch) used by both `search` and `list`.

### `ingest.c` — Ingestor
Detects file extension, delegates to the correct parser, then writes the resulting plain text into `~/.mnemosyne/index/docs/<sha256>.txt` and updates `manifest.json`.

Currently supports: `.txt`, `.md`. `.tex` and `.pdf` are recognised by extension but not yet parsed (v2+).

### `parser/` — Format Parsers
Each parser receives a file path and returns a heap-allocated `char *` of plain text. The caller owns the buffer and frees it.

| File | Handles | Strategy |
|---|---|---|
| `txt.c` | `.txt` | `fread` directly |
| `md.c` | `.md` | strip formatting markers; lowercase output; emit `[LIST]`, `[LINK]` tokens |
| `parser.c` | dispatch | routes to the correct parser by `FileType` |

### `index.c` — Index Store
Reads and writes `manifest.json`. Each entry:

```json
{
  "original_path": "/home/user/notes.txt",
  "hash": "a3f5c9...",
  "size_bytes": 4096,
  "last_modified": 1718400000,
  "file_type": "txt",
  "repository": "/home/user/myproject"
}
```

Functions: `index_add()`, `index_remove()`, `index_get_entries()`.

### `search.c` — Keyword Matcher (v1)
Iterates over all `docs/<hash>.txt` files. For each, counts occurrences of the (already-lowercased) query string using `strstr()`. Builds a result list sorted by recency then match count. Provides a 256-character context snippet centred on the first match.

### `config.c` — Config Manager
Reads and writes `~/.mnemosyne.conf` — a plain-text file with two lines: the data directory path and the IDE key.

### `remove.c` — Index Removal
Removes an entry from `manifest.json` and deletes the corresponding `docs/<hash>.txt` file.

### `init.c` — First-time Setup
Prompts for storage location and IDE on first run, creates the index directory structure, and writes the initial `~/.mnemosyne.conf`.

---

## Source File Structure

```
Mnemosyne/
├── src/
│   ├── main.c
│   ├── command_handler.c
│   ├── command_handler.h
│   ├── ingest.c
│   ├── ingest.h
│   ├── index.c
│   ├── index.h
│   ├── search.c
│   ├── search.h
│   ├── remove.c
│   ├── remove.h
│   ├── config.c
│   ├── config.h
│   ├── init.c
│   ├── init.h
│   ├── help.c
│   ├── help.h
│   ├── types.h
│   ├── sha256.c
│   ├── sha256.h
│   ├── cJSON.c
│   ├── cJSON.h
│   └── parser/
│       ├── parser.c
│       ├── parser.h
│       ├── txt.c
│       ├── txt.h
│       ├── md.c
│       └── md.h
├── documentation/
│   ├── structure.md      ← this file
│   ├── commands.md
│   ├── file-types.md
│   ├── development.md
│   └── roadmap.md
├── Makefile
├── build.bat
├── .gitignore
└── README.md
```

---

## Runtime Data Layout

```
~/.mnemosyne.conf          ← IDE key and data directory path (plain text)

~/.mnemosyne/              ← default data directory (configurable)
└── index/
    ├── manifest.json
    └── docs/
        ├── a3f5c9d2....txt
        ├── b81e04f7....txt
        └── ...
```

- Each `docs/<hash>.txt` contains the extracted plain-text of one document.
- The hash is SHA-256 of the original file's absolute path (not its content), so re-indexing the same path overwrites the same slot.
- `manifest.json` is the only file that maps hashes back to original paths and metadata.
