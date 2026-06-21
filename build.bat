@echo off

gcc -Wall -Wextra -std=c11 -Isrc src/main.c src/index.c src/help.c src/cJSON.c src/command_handler.c src/init.c src/config.c src/sha256.c src/parser/txt.c src/parser/md.c src/parser/parser.c src/ingest.c src/search.c src/remove.c src/picker.c -o mn.exe

if %errorlevel% == 0 (
    echo Build successful: mn.exe
) else (
    echo Build failed.
)
