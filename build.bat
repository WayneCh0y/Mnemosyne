@echo off
gcc -Wall -Wextra -std=c11 -Isrc src/main.c src/index.c src/help.c src/cJSON.c src/command_handler.c src/init.c src/sha256.c src/parser/txt.c src/parser/parser.c src/ingest.c -o mnemosyne.exe
if %errorlevel% == 0 (
    echo Build successful: mnemosyne.exe
) else (
    echo Build failed.
)
