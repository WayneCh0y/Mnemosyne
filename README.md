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

Windows doesn't ship with `gcc` or `make`. Follow these steps:

**Step 1 — Install MSYS2**

Download and run the installer from [msys2.org](https://www.msys2.org/). This gives you a MinGW toolchain with `gcc` and `make`.

**Step 2 — Install the build tools**

Open the **MSYS2 UCRT64** shell (from the Start menu) and run:
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
```

**Step 3 — Add MSYS2 to your Windows PATH**

Run this once in PowerShell, then open a new terminal:
```powershell
[Environment]::SetEnvironmentVariable('PATH', $env:PATH+';C:\msys64\ucrt64\bin', 'User')
```

**Step 4 — Build and install**

From PowerShell, in the Mnemosyne project directory:
```powershell
mingw32-make
mingw32-make install
```
> On Windows, the binary installed by `pacman` is called `mingw32-make` (not `make`). Use it everywhere this README says `make`.

Copies `mnemosyne.exe` to `%USERPROFILE%\bin\`. If that folder is not yet on your PATH, run this once then open a new terminal:
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

On first run, Mnemosyne prompts you for a storage location and a default IDE. You can press Enter to accept the default storage path; the IDE is chosen from an arrow-key picker.

```bash
# add some files
mnemosyne add notes.txt
mnemosyne add ~/Documents/thesis.pdf

# search
mnemosyne search simplex

# change your default IDE later (opens a picker)
mnemosyne config ide

# or set it directly by name
mnemosyne config ide nvim
```

Supported IDE keys: `code`, `cursor`, `nvim`, `vim`, `nano`, `idea`.
