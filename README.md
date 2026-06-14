# Mnemosyne

A command-line tool that lets you search all your personal files and notes instantly — like a local Google for your computer.

```
recall add thesis.pdf
recall search "simplex algorithm"
```

Named after the Greek goddess of memory.

---

## Documentation

- [System Architecture](documentation/structure.md) — component diagram, module descriptions, source and data layouts
- [Command Reference](documentation/commands.md) — all commands with examples
- [Supported File Types](documentation/file-types.md) — `.txt`, `.md`, `.tex`, `.pdf` parsing details
- [Roadmap](documentation/roadmap.md) — v1 direct match → v4 semantic search

## Building

```bash
make          # builds ./recall
make install  # copies to /usr/local/bin
make test     # runs unit tests
```

Requires `pdftotext` (poppler) for PDF support. See [file-types.md](documentation/file-types.md) for installation instructions.

## Quick Start

```bash
# configure your IDE (once)
recall config ide code

# add some files
recall add notes.txt
recall add ~/Documents/thesis.pdf

# search
recall search simplex
```
