# Mnemosyne

A command-line tool to instantly find and open any of your files from anywhere — search by content or browse your full index, and jump straight to the result in your configured IDE.

```
mnemosyne add thesis.pdf
mnemosyne search "simplex algorithm"
```

Named after the Greek goddess of memory.

---

## Contents

- [Documentation](#documentation)
- [Building](#building)
- [Installation](#installation)
  - [Linux](#linux)
  - [macOS](#macos)
  - [Windows](#windows)
  - [Enabling GUI IDE launchers](#enabling-gui-ide-launchers)
  - [Uninstalling](#uninstalling)
- [Quick Start](#quick-start)

## Documentation

- [System Architecture](documentation/structure.md) — component diagram, module descriptions, source and data layouts
- [Command Reference](documentation/commands.md) — all commands with examples
- [Supported File Types](documentation/file-types.md) — `.txt`, `.md`, `.tex`, `.pdf` parsing details
- [Roadmap](documentation/roadmap.md) — v1 direct match → v4 semantic search

## Building

```bash
make        # build the binary
make clean  # remove the binary
```

## Installation

Install `mnemosyne` to your PATH so you can run it from anywhere.

### Linux

**Prerequisites** — `gcc` and `make` (usually pre-installed; otherwise install via your distro's package manager).

**Build and install:**
```bash
sudo make install                      # installs to /usr/local/bin
make install PREFIX=$HOME/.local       # no-sudo alternative (~/.local/bin)
```

**Add to PATH** (no-sudo path only) — `~/.local/bin` may not be on PATH:
```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc   # or ~/.zshrc
```
Open a new terminal afterwards.

### macOS

**Prerequisites** — Apple Command Line Tools (not the full Xcode IDE):
```bash
xcode-select --install
```
> `gcc` on macOS is aliased to Apple Clang — the build works as-is.

**Build and install:**
```bash
sudo make install                      # installs to /usr/local/bin
make install PREFIX=$HOME/.local       # no-sudo alternative (~/.local/bin)
```

**Add to PATH** (no-sudo path only) — `~/.local/bin` is not on PATH by default:
```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc
```
Open a new terminal afterwards.

### Windows

**Prerequisites** — MSYS2 with the MinGW toolchain (provides `gcc` and `make`):

1. Download and run the installer from [msys2.org](https://www.msys2.org/).
2. In the **MSYS2 UCRT64** shell, install the build tools:
   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
   ```
3. Add MSYS2 to your Windows PATH (run in PowerShell, then open a new terminal):
   ```powershell
   [Environment]::SetEnvironmentVariable('PATH', $env:PATH+';C:\msys64\ucrt64\bin', 'User')
   ```

**Build and install** (PowerShell, from the project directory):
```powershell
mingw32-make
mingw32-make install
```
> On Windows the binary is `mingw32-make`, not `make`. Use it wherever this README says `make`.

This copies `mnemosyne.exe` to `%USERPROFILE%\bin\`.

**Add to PATH** (if `%USERPROFILE%\bin` isn't already there):
```powershell
[Environment]::SetEnvironmentVariable('PATH', $env:PATH+";$env:USERPROFILE\bin", 'User')
```
Open a new terminal afterwards.
> Do not use `setx` to add to PATH — it truncates paths longer than 1024 characters, which can silently break other tools.

### Enabling GUI IDE launchers

For `mnemosyne` to open files in `code`, `cursor`, or `idea`, the matching CLI launcher must be on your PATH. Windows and Linux installers usually handle this automatically. On **macOS**, it's a manual step:

| IDE key | How to enable on macOS |
|---|---|
| `code` | Open VS Code → `Cmd+Shift+P` → run **Shell Command: Install 'code' command in PATH** |
| `cursor` | Open Cursor → `Cmd+Shift+P` → run **Shell Command: Install 'cursor' command in PATH** |
| `idea` | Open IntelliJ IDEA → **Tools → Create Command-Line Launcher** (or use JetBrains Toolbox → Settings → *Generate shell scripts*) |

Open a new terminal afterwards, then verify with `code --version`, `cursor --version`, or `idea --version`.

`nvim`, `vim`, and `nano` are installed via package managers and are on PATH automatically.

### Uninstalling
```bash
sudo make uninstall          # Linux/macOS if installed to /usr/local/bin
make uninstall               # Linux/macOS no-sudo install
mingw32-make uninstall       # Windows
```

## Quick Start

On first run, Mnemosyne prompts you for a storage location and a default IDE. You can press Enter to accept the default storage path; the IDE is chosen from an interactive picker (arrow keys or type a number to jump to an option).

```bash
# add some files
mnemosyne add notes.txt
mnemosyne add ~/Documents/thesis.md

# search
mnemosyne search simplex

# change your default IDE later (opens a picker)
mnemosyne config ide

# or set it directly by name
mnemosyne config ide nvim

# browse all indexed files interactively
mnemosyne list
```

Supported IDE keys: `code`, `cursor`, `nvim`, `vim`, `nano`, `idea`. See [Enabling GUI IDE launchers](#enabling-gui-ide-launchers) if `code`/`cursor`/`idea` aren't found on macOS.
