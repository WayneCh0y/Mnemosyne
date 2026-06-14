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

Searches all indexed documents for the given keyword or phrase. Results are ranked by match count and displayed in an interactive picker. Selecting a result opens the original file in your configured IDE.

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

**Interactive picker**

After running the command, you see a numbered result list:
```
Found 3 matches for "simplex":

  [1] notes.txt                     5 matches
      "...the simplex algorithm is used for linear programming..."

  [2] chat1.txt                     2 matches
      "...we discussed the simplex method for LP..."

  [3] project/design.md             1 match
      "...optimization using simplex..."

Select [1-3] or q to quit: _
```

- Type a number and press Enter to open that file.
- Type `q` and press Enter to cancel.
- The file opens in the IDE set by `mnemosyne config ide`.

**If no IDE is configured**, `mnemosyne` prints an error asking you to run `mnemosyne config ide <name>` first.

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

Sets the IDE that `mnemosyne search` opens files in.

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

The setting is saved to `~/.mnemosyne/config.json` and persists across sessions.

---

## Exit Codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | User error (bad arguments, file not found, unsupported type) |
| `2` | Internal error (index corruption, write failure) |
