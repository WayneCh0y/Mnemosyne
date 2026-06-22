CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc

# Detect OS, then on Windows detect which shell make is using.
# Strawberry Perl / GnuWin32 make use cmd.exe; MSYS2 / MinGW / Git Bash make use sh.
# Probe: `echo "x"` keeps the quotes in cmd, strips them in sh.
ifeq ($(OS), Windows_NT)
    ifeq ($(shell echo "x"),"x")
        SHELL_KIND  := cmd
        TARGET       = mn.exe
        DEL          = del /Q
        INSTALL_DIR  = $(USERPROFILE)\bin
        COPY         = copy /Y
    else
        SHELL_KIND  := sh
        TARGET       = mn.exe
        DEL          = rm -f
        INSTALL_DIR  = $(subst \,/,$(USERPROFILE))/bin
        COPY         = cp -f
    endif
else
    TARGET      = mn
    DEL         = rm -f
    PREFIX     ?= /usr/local
    INSTALL_DIR = $(PREFIX)/bin
    COPY        = install -m 755
endif

SRCS = src/main.c \
       src/help.c \
       src/command_handler.c \
       src/picker.c \
       src/init.c \
       src/config.c \
       src/sha256.c \
       src/parser/txt.c \
       src/parser/md.c \
       src/parser/parser.c \
       src/ingest.c \
       src/index.c \
       src/search.c \
       src/remove.c \
       src/workspace.c \
       src/app_resolve.c \
       src/cJSON.c

ifeq ($(OS), Windows_NT)
    LDFLAGS = -ladvapi32 -lshell32
else
    LDFLAGS =
endif

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

# Debug build with sanitizers — Linux/macOS only (not supported by MinGW on Windows)
sanitize: $(SRCS)
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SRCS) -o $(TARGET)

install: $(TARGET)
ifeq ($(SHELL_KIND),cmd)
	@if not exist "$(INSTALL_DIR)" mkdir "$(INSTALL_DIR)"
	$(COPY) "$(TARGET)" "$(INSTALL_DIR)\$(TARGET)"
	@powershell -NoProfile -Command " \
		$$cur = [Environment]::GetEnvironmentVariable('PATH','User'); \
		if ($$cur -notlike '*$(INSTALL_DIR)*') { \
			[Environment]::SetEnvironmentVariable('PATH', $$cur + ';$(INSTALL_DIR)', 'User'); \
			Write-Host 'Added $(INSTALL_DIR) to user PATH.'; \
		} else { \
			Write-Host '$(INSTALL_DIR) already in user PATH.'; \
		}"
	@echo Installed to $(INSTALL_DIR)\$(TARGET)
	@echo Open a new terminal window for PATH changes to take effect.
else ifeq ($(SHELL_KIND),sh)
	@mkdir -p "$(INSTALL_DIR)"
	$(COPY) "$(TARGET)" "$(INSTALL_DIR)/$(TARGET)"
	@powershell.exe -NoProfile -Command " \
		\$$dir = '$(INSTALL_DIR)'.Replace('/', '\\'); \
		\$$cur = [Environment]::GetEnvironmentVariable('PATH','User'); \
		if (\$$cur -notlike ('*' + \$$dir + '*')) { \
			[Environment]::SetEnvironmentVariable('PATH', \$$cur + ';' + \$$dir, 'User'); \
			Write-Host ('Added ' + \$$dir + ' to user PATH.'); \
		} else { \
			Write-Host (\$$dir + ' already in user PATH.'); \
		}"
	@echo "Installed to $(INSTALL_DIR)/$(TARGET)"
	@echo "Open a new terminal window for PATH changes to take effect."
else
	install -d $(INSTALL_DIR)
	$(COPY) $(TARGET) $(INSTALL_DIR)/$(TARGET)
	@echo "Installed to $(INSTALL_DIR)/$(TARGET)"
	@echo "If using /usr/local/bin, you may need: sudo make install"
	@echo "For a no-sudo install: make install PREFIX=\$$HOME/.local"
endif

uninstall:
ifeq ($(SHELL_KIND),cmd)
	@if exist "$(INSTALL_DIR)\$(TARGET)" $(DEL) "$(INSTALL_DIR)\$(TARGET)"
	@echo Removed $(INSTALL_DIR)\$(TARGET)
else
	$(DEL) "$(INSTALL_DIR)/$(TARGET)"
	@echo "Removed $(INSTALL_DIR)/$(TARGET)"
endif

clean:
	$(DEL) $(TARGET)

.PHONY: clean sanitize install uninstall
