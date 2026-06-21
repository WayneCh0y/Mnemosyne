# Development Guide

## Compiling the Project

### Windows

Run the build script from the project root:

```bat
.\build.bat
```

This produces `mn.exe` in the project root. Run it with:

```bat
.\mn.exe
```

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

When you create a new module (e.g. `src/config.c`), register it in both build systems:

**build.bat** — add the file to the `gcc` line:
```bat
gcc -Wall -Wextra -std=c11 -Isrc src/main.c src/help.c src/config.c -o mn.exe
```

**Makefile** — add it to the `SRCS` variable:
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
├── build.bat     # Windows build script
├── Makefile      # Linux/WSL build script
└── mn.exe / mn                 # compiled binary (not committed to git)
```
