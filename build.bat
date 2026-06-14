@echo off
gcc -Wall -Wextra -std=c11 -Isrc src/main.c src/help.c src/command_handler.c -o mnemosyne.exe
if %errorlevel% == 0 (
    echo Build successful: mnemosyne.exe
) else (
    echo Build failed.
)
