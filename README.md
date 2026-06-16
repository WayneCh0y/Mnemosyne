# Mnemosyne

A command-line tool that lets you search all your personal files and notes instantly — like a local Google for your computer.

```
mnemosyne add thesis.pdf
mnemosyne search "simplex algorithm"
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
make          # builds ./mnemosyne binary
make install  # copies to /usr/local/bin
make test     # runs unit tests
```

Requires `pdftotext` (poppler) for PDF support. See [file-types.md](documentation/file-types.md) for installation instructions.

## Quick Start

On first run, Mnemosyne prompts you for a storage location and a default IDE — you can press Enter to accept the defaults.

```bash
# add some files
mnemosyne add notes.txt
mnemosyne add ~/Documents/thesis.pdf

# search
mnemosyne search simplex

# change your default IDE later
mnemosyne config ide nvim
```

Supported IDE keys: `code`, `cursor`, `nvim`, `vim`, `nano`, `idea`.
