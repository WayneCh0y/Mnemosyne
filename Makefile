CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc

# Detect OS — set executable name and delete command accordingly
ifeq ($(OS), Windows_NT)
    TARGET      = mnemosyne.exe
    DEL         = del /Q
    INSTALL_DIR = $(USERPROFILE)\bin
    COPY        = copy /Y
else
    TARGET      = mnemosyne
    DEL         = rm -f
    PREFIX      ?= /usr/local
    INSTALL_DIR  = $(PREFIX)/bin
    COPY         = install -m 755
endif

SRCS = src/main.c \
       src/help.c \
       src/command_handler.c \
       src/init.c \
       src/config.c \
       src/sha256.c \
       src/parser/txt.c \
       src/parser/md.c \
       src/parser/parser.c \
       src/ingest.c \
       src/index.c \
       src/search.c \
       src/cJSON.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# Debug build with sanitizers — Linux/macOS only (not supported by MinGW on Windows)
sanitize: $(SRCS)
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SRCS) -o $(TARGET)

install: $(TARGET)
ifeq ($(OS), Windows_NT)
	@if not exist "$(INSTALL_DIR)" mkdir "$(INSTALL_DIR)"
	$(COPY) $(TARGET) "$(INSTALL_DIR)\$(TARGET)"
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
else
	install -d $(INSTALL_DIR)
	$(COPY) $(TARGET) $(INSTALL_DIR)/$(TARGET)
	@echo "Installed to $(INSTALL_DIR)/$(TARGET)"
	@echo "If using /usr/local/bin, you may need: sudo make install"
	@echo "For a no-sudo install: make install PREFIX=\$$HOME/.local"
endif

uninstall:
ifeq ($(OS), Windows_NT)
	@if exist "$(INSTALL_DIR)\$(TARGET)" $(DEL) "$(INSTALL_DIR)\$(TARGET)"
	@echo Removed $(INSTALL_DIR)\$(TARGET)
else
	$(DEL) $(INSTALL_DIR)/$(TARGET)
	@echo "Removed $(INSTALL_DIR)/$(TARGET)"
endif

clean:
	$(DEL) $(TARGET)

.PHONY: clean sanitize install uninstall
