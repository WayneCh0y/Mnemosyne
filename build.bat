@echo off
gcc -Wall -Wextra -std=c11 -Isrc src/main.c src/help.c -o mnemosyne.exe
if %errorlevel% == 0 (
    echo Build successful: mnemosyne.exe
) else (
    echo Build failed.
)
