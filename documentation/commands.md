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

**Interactive controls**

| Key | Action |
|---|---|
| `↑` / `↓` | Move selection up or down |
| `Enter` | Open the selected file in the configured IDE |
| `Esc` | Exit without opening anything |
| `1`–`9` | Enter numeric jump mode — type the index number shown in brackets |
| `0`–`9` | Append a digit (once a non-zero digit has been typed) |
| `Backspace` | Erase the last digit; shows `-` when all digits are cleared |
| `Enter` (numeric mode, valid index) | Jump directly to that result |
| `Enter` (numeric mode, invalid index) | Show `no such index!` and return to arrow-key mode |
| `Esc` / `↑` / `↓` (numeric mode) | Return to arrow-key mode |

---

## `mnemosyne list`

Opens an interactive picker showing all files currently in the index. Navigate with the arrow keys, press Enter to open the selected file in your configured IDE, or Esc to exit without opening anything.

**Usage**
```
mnemosyne list
```

**Interactive controls**

| Key | Action |
|---|---|
| `↑` / `↓` | Move selection up or down |
| `Enter` | Open the selected file in the configured IDE |
| `Esc` | Exit without opening anything |
| `1`–`9` | Enter numeric jump mode — type the index number shown in brackets |
| `0`–`9` | Append a digit (once a non-zero digit has been typed) |
| `Backspace` | Erase the last digit; shows `-` when all digits are cleared |
| `Enter` (numeric mode, valid index) | Jump directly to that entry |
| `Enter` (numeric mode, invalid index) | Show `no such index!` and return to arrow-key mode |
| `Esc` / `↑` / `↓` (numeric mode) | Return to arrow-key mode |

If no files are indexed, prints `No files indexed.` and exits.

**Opening behaviour**

Identical to `search`: if the file belongs to a git repository, VS Code and Cursor are opened with the repository root as the workspace and the file as the target (`--goto`). IntelliJ IDEA receives both the repository root and the file path. All other IDEs receive the file path only.

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

## `mnemosyne config ide [name]`

Changes the IDE that `mnemosyne search` and `mnemosyne list` open files in. The initial value is set during first-time setup (see below); use this command to change it later.

**Usage**
```
mnemosyne config ide            # interactive picker
mnemosyne config ide <name>     # set directly by name
```

Running with no argument opens an interactive picker. Use ↑/↓ to navigate, Enter to confirm, Esc to cancel without changing the current setting. You can also type a number (`1`–`9`) to jump directly to that option by index, then press Enter to confirm.

**Supported IDE keys**

| Key | Opens with |
|---|---|
| `code` | Visual Studio Code |
| `cursor` | Cursor |
| `nvim` | Neovim |
| `vim` | Vim |
| `nano` | Nano |
| `idea` | IntelliJ IDEA |

**Examples**
```
mnemosyne config ide
mnemosyne config ide code
mnemosyne config ide nvim
```

When passed a name, an invalid key prints the list of supported options and exits without changing anything.

The setting is saved to `~/.mnemosyne.conf` (plain text, one value per line) and persists across sessions.

---

## First-time setup

The first time you run any `mnemosyne` command, you are prompted for:

1. **Storage location** — where to keep the index. Defaults to `~/.mnemosyne` (press Enter to accept).
2. **Default IDE** — which editor `search` results open in. Choose from an interactive picker: ↑/↓ to navigate, Enter to confirm, or type a number to jump directly to an option.

Both values are saved to `~/.mnemosyne.conf` and reused on every subsequent run. To re-run setup, delete `~/.mnemosyne.conf`.

---

## Exit Codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | User error (bad arguments, file not found, unsupported type) |
| `2` | Internal error (index corruption, write failure) |
