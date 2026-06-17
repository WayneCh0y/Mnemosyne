# Command Reference

All commands are issued through the `mnemosyne` binary.

---

## `mnemosyne add <file>`

Ingests a file into the local index. The file's text is extracted, stored under `~/.mnemosyne/index/docs/`, and its metadata is recorded in `manifest.json`.

**Usage**
```
mnemosyne add <path-to-file>
```

**Examples**
```
mnemosyne add notes.txt
mnemosyne add ~/Documents/thesis.pdf
mnemosyne add project/design.md
```

**Supported file types:** `.txt`, `.md`, `.tex`, `.pdf`
See [file-types.md](file-types.md) for how each format is parsed.

**Behaviour**
- If the file has already been indexed, it is re-indexed (content refreshed).
- Unsupported file extensions print an error and exit with code 1.
- Non-existent paths print an error and exit with code 1.

---

## `mnemosyne search <query>`

Searches all indexed documents for the given keyword or phrase. The query is case-insensitive. Modified files are automatically re-indexed before searching. Up to 5 results are shown, ranked by recency then match count.

**Usage**
```
mnemosyne search <query>
```

**Examples**
```
mnemosyne search simplex
mnemosyne search "linear programming"
mnemosyne search docker compose
```

**Output format**

Each result shows the original file path and a context snippet of up to 256 characters centered on the first match (25% before, 75% after), with `...` where the file extends beyond the window:

```
[1] /home/user/notes.txt
    ...the simplex algorithm is used for linear...
[2] /home/user/project/design.md
    ...optimization using simplex methods across...
```

For `.md` files, the context is rendered with formatting:
- Headings appear in **magenta**
- List items appear as `- item one` / `- item two` (one per line)
- Links appear as `link` in **blue**

---

## `mnemosyne list`

Lists all files currently in the index.

**Usage**
```
mnemosyne list
```

**Example output**
```
Indexed files (3):

  notes.txt                  [txt]   4.1 KB   2026-06-10
  ~/Documents/thesis.pdf     [pdf]  312.0 KB   2026-06-12
  project/design.md          [md]    8.7 KB   2026-06-14
```

---

## `mnemosyne remove <file>`

Removes a file from the index. Does not delete the original file.

**Usage**
```
mnemosyne remove <path-to-file>
```

**Example**
```
mnemosyne remove notes.txt
```

Prints an error if the file is not currently indexed.

---

## `mnemosyne config ide <name>`

Changes the IDE that `mnemosyne search` opens files in. The initial value is set during first-time setup (see below); use this command to change it later.

**Usage**
```
mnemosyne config ide <name>
```

**Supported IDE keys**

| Key | Opens with |
|---|---|
| `code` | Visual Studio Code |
| `cursor` | Cursor |
| `nvim` | Neovim |
| `vim` | Vim |
| `nano` | Nano |
| `idea` | IntelliJ IDEA |

**Example**
```
mnemosyne config ide code
mnemosyne config ide nvim
```

An invalid key prints the list of supported options and exits without changing anything.

The setting is saved to `~/.mnemosyne.conf` (plain text, one value per line) and persists across sessions.

---

## First-time setup

The first time you run any `mnemosyne` command, you are prompted for:

1. **Storage location** — where to keep the index. Defaults to `~/.mnemosyne`.
2. **Default IDE** — which editor `search` results open in. Defaults to `code`.

Pressing Enter at either prompt accepts the default. Both values are saved to `~/.mnemosyne.conf` and reused on every subsequent run. To re-run setup, delete `~/.mnemosyne.conf`.

---

## Exit Codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | User error (bad arguments, file not found, unsupported type) |
| `2` | Internal error (index corruption, write failure) |
