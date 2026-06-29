# Development Guide

## Compiling the Project

### Windows

Build via the Makefile using MinGW's `make` (prerequisites are in [README → Windows](../README.md#windows)):

```powershell
mingw32-make
```

This produces `mn.exe` in the project root. Run it with:

```powershell
.\mn.exe
```

> The legacy `build.bat` in the project root is kept for reference but is no longer maintained — it lists an outdated subset of source files. Use `mingw32-make` instead.

### Linux / WSL

Install dependencies if you haven't already:

```bash
sudo apt install make gcc
```

Then build with:

```bash
make
```

This produces `mn` in the project root. Run it with:

```bash
./mn
```

To delete the compiled binary and do a clean build:

```bash
make clean
make
```

---

## Building with Memory Sanitizers (Linux / WSL only)

Sanitizers catch memory bugs at runtime — buffer overflows, use-after-free, undefined behavior — and print a detailed error report when one is triggered.

```bash
make sanitize
./mn
```

Not supported on Windows with MinGW. Use WSL to run sanitizer builds.

---

## Adding a New Source File

When you create a new module (e.g. `src/config.c`), register it in the `SRCS` variable of the Makefile:

```makefile
SRCS = src/main.c \
       src/help.c \
       src/config.c
```

---

## Project Structure

```
Mnemosyne/
├── src/          # all source files (.c and .h)
├── documentation/
├── Makefile      # build script (Linux/macOS/WSL: make; Windows: mingw32-make)
└── mn.exe / mn                 # compiled binary (not committed to git)
```
