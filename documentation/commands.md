# Command Reference

All commands are issued through the `mn` binary.

---

## `mn add <file-or-folder>`

Ingests a file (or every supported file inside a folder) into the local index. Each file's text is extracted, stored under `~/.mnemosyne/index/docs/`, and its metadata is recorded in `manifest.json`.

**Usage**
```
mn add <path-to-file>
mn add <path-to-folder>
```

**Examples**
```
mn add notes.txt
mn add ~/Documents/thesis.pdf
mn add project/design.md
mn add ~/Documents/notes        # adds every supported file under notes/
```

**Supported file types:** `.txt`, `.md`, `.pdf` (`.tex` is recognised but not yet parsed).
See [file-types.md](file-types.md) for how each format is parsed.

**Behaviour**
- If the file has already been indexed, it is re-indexed (content refreshed).
- When passed a folder, `mn add` walks it recursively (up to 8 levels deep) and indexes every file with a supported extension. Files with unsupported extensions inside the folder are silently skipped — no error noise from unrelated files like `.gitignore` or `.png`.
- Unsupported file extensions on a *direct* `mn add <file>` print an error and exit with code 1.
- Non-existent paths print an error and exit with code 1.
- `.pdf` ingestion requires the `pdftotext` binary (poppler-utils). If it's not found, `mn add` prints a per-OS install hint and skips the file — all other file types continue to work. See [Enabling PDF support](../README.md#enabling-pdf-support).

---

## `mn search <query>`

Searches all indexed documents for the given keyword or phrase. The query is case-insensitive by default and matches whole words only — `"for"` will not match `"format"` or `"before"`. Pass `-c` to require an exact-case match. Modified files are automatically re-indexed before searching.

Searches also match file paths using segment matching — `"README.md"`, `"Mnemosyne/"`, and `"C:/Users/Wayne"` each return matching files, but `"Proj"` does not match `"Projects"` and `"README"` does not match `"README.md"`. Path matching follows the same case rule as content matching: insensitive by default, exact case with `-c`. When a file is found by path, the start of the document is shown as the preview. Up to 5 results are shown, ranked by recency then match count.

**Usage**
```
mn search [-c] <query>
```

The flag can appear anywhere in the command — before, after, or between query words.

**Examples**
```
mn search simplex
mn search "linear programming"
mn search docker compose
mn search -c TODO                  # only matches "TODO", not "todo" or "Todo"
mn search "linear programming" -c
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

## `mn list`

Opens an interactive picker showing all files currently in the index. Navigate with the arrow keys, press Enter to open the selected file in your configured IDE, or Esc to exit without opening anything.

**Usage**
```
mn list
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

## `mn remove [file-or-folder]`

Removes a file (or every indexed file under a folder) from the index. Does not delete the original file — re-add it any time with `mn add`.

**Usage**
```
mn remove                   # interactive picker over all indexed files
mn remove <path-to-file>    # remove a specific file directly
mn remove <path-to-folder>  # remove every indexed file under that folder
```

`mn remove` (no arguments) opens the same picker UI as `mn list` (titled "Remove a file"); arrow keys or `1`–`9` to select, **Enter to remove**, `Esc` to cancel. Removing drops both the index entry and the cached text copy.

**Examples**
```
mn remove notes.txt
mn remove ~/Documents/notes       # drops every indexed file under notes/
```

**Behaviour**
- The picker form (no arguments) removes by the stored path, so files that have been **deleted from disk** can still be selected and removed (they're also auto-pruned from the index on the next `mn search`). The direct `mn remove <file>` form prints an error if the path can't be resolved or isn't indexed.
- The folder form matches by **path prefix**: every manifest entry whose `original_path` starts with `<folder>/` (or `<folder>\` on Windows) is removed. A trailing-separator guard ensures `mn remove ~/foo` does **not** also wipe entries under `~/foo_archive/`.
- The folder form does **not** walk the disk — it operates entirely on the manifest. So a folder that's been deleted from disk can still be cleaned out of the index in one call, as long as the path resolves (on Linux/macOS, deleted folders may fail to resolve via `realpath`; on Windows, `_fullpath` normalizes paths without requiring existence).
- A summary is printed: `Removed N files under <folder>` (or `No indexed files under <folder>` if nothing matched).

---

## `mn reindex`

Re-parses every indexed file from disk and rewrites its cached plain-text copy. Useful after a parser change, after a `docs/` file has been manually deleted, or to refresh the whole index in one shot. Entries whose original file no longer exists on disk are dropped (same rule as the auto-prune that runs before every `mn search`).

**Usage**
```
mn reindex
```

Takes no arguments.

**Behaviour**
- Walks every entry in `manifest.json`. For each: if the file is missing, the manifest entry and its cached doc are removed; otherwise the file is re-parsed and its entry is updated (same path → same hash, so the existing `docs/<hash>.txt` slot is overwritten).
- Per-file parse failures print to stderr and leave that entry untouched — a transient error won't drop your data.
- Prints a summary on exit: `Reindexed N files.` (with `(M missing files dropped)` appended when applicable).
- If the index is empty, prints `No files indexed yet.` and exits 0.

---

## `mn open`

Manages workspaces — named collections of apps, URLs, and paths to launch all at once.

**Subcommands**

```
mn open                          # interactive picker to choose and launch a workspace
mn open create <name>            # create a new empty workspace
mn open snap                     # snapshot the apps you have open into a new workspace
mn open edit                     # interactive picker to add/remove apps & links in a workspace
```

**Creating and populating a workspace**

```
mn open create work
mn open edit
```

`mn open edit` is fully interactive (same picker UI as the rest of the app) and is where you add **and** remove apps and links. First pick which workspace to edit; pressing `Esc` cancels.

Inside the editor you see the workspace's apps, each with its links beneath it. Changes are **staged** — nothing is written until you press `Enter` to save (`Esc` discards everything):

- **Add an app** — navigate to `[+ Add a new app]` and type a program name (e.g. `chrome`) or a full path (e.g. `C:\Users\you\AppData\Local\Discord\app-1.0\Discord.exe`, `/Applications/Discord.app`, `/usr/bin/foo`); if that value doesn't exist on this machine you'll get a non-blocking warning. New apps show in **green** with a `+`.
- **Add a link** — with the cursor on an app, press any printable key to type a URL/path inline. New links show in **green** with a `+`.
- **Remove an app or a single link** — put the cursor on the app row or on an individual link row and press `Backspace`/`Delete`. An already-saved item is staged for removal and shown in **red** with a `-` (press `Backspace` again to undo); a not-yet-saved (green) item is simply dropped.
- **Remove the whole workspace** — navigate to `[- Remove this workspace]` and press `Enter` or `Backspace`; it highlights red while staged and is deleted when you save.

When typing an app/link, `Backspace` deletes characters and `Esc` cancels that inline edit (it does not exit the editor).

**Snapshotting running apps**

```
mn open snap
```

`mn open snap` builds a workspace from the applications you currently have open, so you don't have to add them one by one. The flow is interactive (`Esc` steps back, cancels at the first step):
1. **Review** — a list of the detected apps, all pre-selected (selected apps show **white text on a blue highlight**; the cursor row is marked with a green `▌` bar). `↑`/`↓` move, `Backspace` toggles the highlighted app between selected (blue) and deselected (dim), `Enter` confirms. Deselect anything you don't want (e.g. the terminal, file-explorer windows).
   - **Add links/targets** — with the cursor on a **selected** app, press any printable key to start typing inline: an `Add link for <app>:` field appears at the bottom of the list (seeded with the key you pressed), where you can type a URL, app link, repository, or file path. `Enter` adds it, `Esc` cancels the edit, `Backspace` deletes. You can repeat to add **as many links to the same app** as you like; each one is shown on its own dim-yellow `→ link` line beneath the app. Deselected (dim) apps don't accept links.
   - **Remove a link** — move the cursor onto a `→ link` line and press `Backspace`; that link is removed immediately. Each link becomes a separate launch entry, so the app opens once per link.
2. **Name** — type a name for the new workspace. If the name already exists you're asked for another.

Apart from any links you set manually, snapshots capture **apps only**, not document/tab state — each remaining entry is saved with no target:

- **Browsers** (Edge/Chrome/…) are saved as the browser itself. Relaunching via `mn open` reopens the browser, which restores its own previous session if it's configured to "continue where you left off". Individual tab URLs are *not* captured (there's no reliable way to read them); add specific URLs by hand with `mn open edit` if you need them pinned.
- The terminal that launched `mn` is excluded automatically.

Platform notes:

| Platform | How running apps are detected |
|---|---|
| **Windows** | Visible top-level windows → their process's full `.exe` path |
| **macOS** | Foreground (non-background) GUI apps by name (launched later with `open -a`) |
| **Linux** | `wmctrl -lp` → each window's process → `/proc/<pid>/exe`. Requires `wmctrl` (install via your package manager); otherwise `mn open snap` reports that it's missing. |

**Removing**

Removal is handled inside `mn open edit` (see *Creating and populating a workspace* above): stage an app, an individual link, or the whole workspace for deletion with `Backspace`/`Delete` (shown in **red** with a `-`), then press `Enter` to save. Nothing is deleted until you save, and `Esc` discards all staged removals.

**Opening a workspace**

`mn open` (with no arguments) shows an interactive picker listing all workspaces with their app count. The **currently highlighted** workspace expands to show its apps in a framed list (long URLs/paths are shown in full, never truncated); other workspaces stay collapsed. Selecting one launches all its entries in sequence.

**Interactive controls**

| Key | Action |
|---|---|
| `↑` / `↓` | Move selection up or down |
| `Enter` | Open the selected workspace |
| `Esc` | Exit without opening anything |
| `1`–`9` | Enter numeric jump mode |
| `Backspace` | Erase last digit |

**Launch behaviour**

`code` / `cursor` are launched by name (they are on `PATH` via their installer). Every other app is the **full executable path** you provided when adding it:

| Platform | `code` / `cursor` | All other apps |
|---|---|---|
| **Windows** | hidden `cmd.exe /c <name> --new-window` (in PATH) | full path launched directly via `ShellExecuteEx` |
| **macOS** | bare name + `--new-window` (CLI in PATH) | `open -a "<full path>"` (returns immediately) |
| **Linux** | bare name + `--new-window` | full path run directly, backgrounded with `&` |

If a launch fails (e.g. the stored path no longer exists), an `error: failed to launch '<app>'` message is printed instead of failing silently.

**Auto-close** — once something is actually opened, `mn` closes the terminal window it was launched from (by terminating the parent shell), leaving just the opened apps. This applies to `mn search` → open, `mn list` → open, and `mn open` → launch. Cancelling the picker with `Esc` opens nothing and leaves the terminal open. The auto-close is skipped when input isn't an interactive terminal (pipes, scripts), so it won't disrupt non-interactive usage.

Workspaces are stored in `~/.mnemosyne/workspaces.json`.

---

## `mn config ide [name]`

Changes the IDE that `mn search` and `mn list` open files in. The initial value is set during first-time setup (see below); use this command to change it later.

**Usage**
```
mn config ide            # interactive picker
mn config ide <name>     # set directly by name
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
mn config ide
mn config ide code
mn config ide nvim
```

When passed a name, an invalid key prints the list of supported options and exits without changing anything.

The setting is saved to `~/.mnemosyne.conf` (plain text, one value per line) and persists across sessions.

---

## First-time setup

The first time you run any `mn` command, you are prompted for:

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
