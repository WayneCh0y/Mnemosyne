# Mnemosyne

A command-line tool that lets you search all your personal files and notes instantly -- like a local Google for your computer.

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
make        # build the binary
make clean  # remove the binary
```

### Installing (so you can type `mnemosyne` from anywhere)

**Linux**
```bash
sudo make install                      # installs to /usr/local/bin
make install PREFIX=$HOME/.local       # no-sudo alternative (~/.local/bin)
```

**macOS**

macOS doesn't ship with `gcc` or `make`. Install Apple's Command Line Tools (not the full Xcode IDE) if you haven't already:
```bash
xcode-select --install
```
Then build and install:
```bash
sudo make install                      # installs to /usr/local/bin
make install PREFIX=$HOME/.local       # no-sudo alternative (~/.local/bin)
```
> `gcc` on macOS is aliased to Apple Clang — this is fine, the build works as-is.

**Windows**

Windows doesn't ship with `gcc` or `make`. Install them via [MSYS2](https://www.msys2.org/) (recommended) or Chocolatey:
```powershell
# MSYS2 (after installing from msys2.org, run in the MSYS2 shell):
pacman -S mingw-w64-ucrt-x86_64-gcc make

# or Chocolatey:
choco install mingw
```
Then, from PowerShell with `gcc` and `make` on your PATH:
```powershell
make install
```
Copies `mnemosyne.exe` to `%USERPROFILE%\bin\`. If that folder is not yet on your PATH, run this once in PowerShell then open a new terminal:
```powershell
[Environment]::SetEnvironmentVariable('PATH', $env:PATH+';C:\Users\<you>\bin', 'User')
```
> **Note:** Do not use `setx` to add to PATH — it truncates paths longer than 1024 characters, which can silently break other tools.

**Uninstalling**
```bash
make uninstall          # Windows / Linux / macOS
sudo make uninstall     # Linux/macOS if installed to /usr/local/bin
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
