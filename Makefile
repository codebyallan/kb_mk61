# Build root for the clean-room keyboard stack.
#
# Layer rules enforced by this file:
#   src/core   -> libkbcore.a / libkbcore.so (no GUI, no global I/O)
#   src/daemon -> kbdd  (links libkbcore; device and uinput owner)
#   src/cli    -> kbctl (links libkbcore)
#
# All C code is C11 and is compiled with -Wall -Wextra -Werror.

SHELL      := /bin/sh

CC         ?= cc
AR         ?= ar
RANLIB     ?= ranlib
INSTALL    ?= install

PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
LIBDIR     ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
UDEVDIR    ?= $(PREFIX)/lib/udev/rules.d
SYSTEMDUSERDIR ?= $(PREFIX)/lib/systemd/user

CSTD       := -std=c11
WARN       := -Wall -Wextra -Werror
OPT        ?= -O2
DEBUG      ?= -g
CFLAGS     ?=
HARDEN     := -fstack-protector-strong -fstack-clash-protection \
              -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
override CFLAGS     += $(CSTD) $(WARN) $(OPT) $(DEBUG) $(HARDEN) -fPIC
override CPPFLAGS   += -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS    ?=
override LDFLAGS    += -Wl,-z,relro,-z,now
EXE_LDFLAGS := -pie
LDLIBS     ?=

BUILD_DIR  := build
OBJ_DIR    := $(BUILD_DIR)/obj
BIN_DIR    := $(BUILD_DIR)/bin
ASAN_DIR   := $(BUILD_DIR)/asan

# Versioning: libkbcore.so.<SOVERSION> is the soname (ABI), libkbcore.so.<VERSION>
# the real file, libkbcore.so the development symlink.  Both come from kbcore.h.
LIB_VERSION    := $(shell sed -nE 's/^\#define KBCORE_VERSION_(MAJOR|MINOR|PATCH)[[:space:]]+([0-9]+)/\2/p' include/kbcore.h | paste -sd.)
LIB_SOVERSION  := $(shell sed -n 's/^\#define KBCORE_SOVERSION \([0-9]*\)/\1/p' include/kbcore.h)
LIB_STATIC     := libkbcore.a
LIB_SHARED     := libkbcore.so
LIB_SONAME     := $(LIB_SHARED).$(LIB_SOVERSION)
LIB_SHARED_REAL := $(LIB_SHARED).$(LIB_VERSION)

CORE_SRCS   := $(wildcard src/core/*.c)
DAEMON_SRCS := $(wildcard src/daemon/*.c)
CLI_SRCS    := $(wildcard src/cli/*.c)
TEST_SRCS   := $(filter-out tests/test_handlers.c tests/test_cli_parse.c tests/test_config.c tests/test_ipc.c, $(wildcard tests/*.c))

CORE_OBJS   := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))
DAEMON_OBJS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(DAEMON_SRCS))
CLI_OBJS    := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(CLI_SRCS))
TEST_OBJS   := $(patsubst tests/%.c,$(OBJ_DIR)/tests/%.o,$(TEST_SRCS))
CLI_PARSER_OBJS := $(OBJ_DIR)/cli/parse.o
CONFIG_OBJS := $(OBJ_DIR)/daemon/config.o
IPC_OBJS := $(OBJ_DIR)/daemon/ipc.o

# ASan/UBSan build (no FORTIFY: it conflicts with the sanitizer runtime).
ASAN_FLAGS      := $(CSTD) $(WARN) -O1 $(DEBUG) -fPIC -fno-omit-frame-pointer \
                   -fsanitize=address,undefined -fno-sanitize-recover=undefined
ASAN_LDFLAGS    := -fsanitize=address,undefined
ASAN_CORE_OBJS  := $(patsubst src/%.c,$(ASAN_DIR)/obj/%.o,$(CORE_SRCS))
ASAN_TEST_OBJS  := $(patsubst tests/%.c,$(ASAN_DIR)/obj/tests/%.o,$(TEST_SRCS))
ASAN_IPC_OBJS := $(ASAN_DIR)/obj/daemon/ipc.o $(ASAN_DIR)/obj/tests/test_ipc.o
ASAN_CONFIG_OBJS := $(ASAN_DIR)/obj/daemon/config.o $(ASAN_DIR)/obj/tests/test_config.o
ASAN_CLI_PARSER_OBJS := $(ASAN_DIR)/obj/cli/parse.o $(ASAN_DIR)/obj/tests/test_cli_parse.o

# Fuzz harness: replayed as a regression suite by `make test` / `make asan`;
# `make fuzz` builds the real libFuzzer binary (clang) and runs it.
FUZZ_CORPUS    := tests/fuzz/corpus
FUZZ_MUTATIONS ?= 20000
FUZZ_CC        ?= clang
FUZZ_TIME      ?= 60
FUZZ_OBJS      := $(OBJ_DIR)/tests/fuzz/fuzz_main.o $(OBJ_DIR)/daemon/ipc.o \
                  $(OBJ_DIR)/daemon/config.o $(CLI_PARSER_OBJS)
ASAN_FUZZ_OBJS := $(ASAN_DIR)/obj/tests/fuzz/fuzz_main.o $(ASAN_DIR)/obj/daemon/ipc.o \
                  $(ASAN_DIR)/obj/daemon/config.o $(ASAN_DIR)/obj/cli/parse.o

DEPFILES := $(FUZZ_OBJS:.o=.d) $(ASAN_FUZZ_OBJS:.o=.d) $(OBJ_DIR)/tests/device/test_device.d $(OBJ_DIR)/tests/test_cli_parse.d $(ASAN_DIR)/obj/tests/device/test_device.d $(ASAN_DIR)/obj/tests/test_cli_parse.d $(CLI_PARSER_OBJS:.o=.d) $(CORE_OBJS:.o=.d) $(DAEMON_OBJS:.o=.d) $(CLI_OBJS:.o=.d) \
            $(TEST_OBJS:.o=.d) $(ASAN_CORE_OBJS:.o=.d) $(ASAN_TEST_OBJS:.o=.d)

.PHONY: all core daemon cli test test-e2e fuzz asan asan-run install clean check-arch help

all: check-arch core daemon cli

# Public library: protocol, profiles and macros.  No GUI and no global I/O.
core: check-arch $(LIB_STATIC) $(LIB_SHARED_REAL)

daemon: check-arch $(BIN_DIR)/kbdd

cli: check-arch $(BIN_DIR)/kbctl

test: check-arch $(BIN_DIR)/kbdd $(BIN_DIR)/kbctl $(BIN_DIR)/fuzz-replay $(BIN_DIR)/test-core $(BIN_DIR)/test-device $(BIN_DIR)/test-handlers $(BIN_DIR)/test-cli $(BIN_DIR)/test-config $(BIN_DIR)/test-ipc
	@echo "==> running unit tests"
	@$(BIN_DIR)/test-core
	@$(BIN_DIR)/test-device
	@$(BIN_DIR)/test-handlers
	@$(BIN_DIR)/test-cli
	@$(BIN_DIR)/test-config
	@$(BIN_DIR)/test-ipc
	@$(BIN_DIR)/fuzz-replay -m $(FUZZ_MUTATIONS) $(FUZZ_CORPUS)
	@$(MAKE) --no-print-directory test-e2e

# End-to-end: real kbdd + kbctl over the socket, no keyboard attached.
test-e2e: check-arch $(BIN_DIR)/kbdd $(BIN_DIR)/kbctl
	@echo "==> running e2e tests"
	@sh tests/integration/test_e2e.sh $(BIN_DIR)

# Coverage-guided fuzzing (libFuzzer needs clang): make fuzz FUZZ_TIME=300
fuzz: check-arch
	@mkdir -p $(BUILD_DIR)/fuzz/work
	$(FUZZ_CC) $(CSTD) -O1 $(DEBUG) -fno-omit-frame-pointer \
		-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
		-DFUZZ_LIBFUZZER $(CPPFLAGS) -o $(BUILD_DIR)/fuzz/fuzz \
		tests/fuzz/fuzz_main.c src/daemon/ipc.c src/daemon/config.c \
		src/cli/parse.c $(CORE_SRCS)
	@cp -n $(FUZZ_CORPUS)/* $(BUILD_DIR)/fuzz/work/ 2>/dev/null || true
	$(BUILD_DIR)/fuzz/fuzz -max_total_time=$(FUZZ_TIME) -timeout=10 \
		-artifact_prefix=$(BUILD_DIR)/fuzz/ $(BUILD_DIR)/fuzz/work

asan: check-arch
	@mkdir -p $(BUILD_DIR)
	@printf 'int main(void){return 0;}\n' > $(BUILD_DIR)/.san-probe.c
	@if $(CC) $(CSTD) $(ASAN_LDFLAGS) $(BUILD_DIR)/.san-probe.c \
		-o $(BUILD_DIR)/.san-probe >/dev/null 2>&1; then \
		rm -f $(BUILD_DIR)/.san-probe.c $(BUILD_DIR)/.san-probe; \
		$(MAKE) --no-print-directory asan-run; \
	else \
		rm -f $(BUILD_DIR)/.san-probe.c $(BUILD_DIR)/.san-probe; \
		echo "ERROR: ASan/UBSan runtime unavailable; install it (e.g. libasan/libubsan) to run 'make asan'." >&2; \
		exit 1; \
	fi

asan-run: check-arch $(ASAN_DIR)/fuzz-replay $(ASAN_DIR)/test-core $(ASAN_DIR)/test-device $(ASAN_DIR)/test-handlers $(ASAN_DIR)/test-cli $(ASAN_DIR)/test-config $(ASAN_DIR)/test-ipc
	@echo "==> running ASan/UBSan tests"
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	 $(ASAN_DIR)/test-core
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	 $(ASAN_DIR)/test-device
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	 $(ASAN_DIR)/test-handlers
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	 $(ASAN_DIR)/test-cli
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	 $(ASAN_DIR)/test-config
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	 $(ASAN_DIR)/test-ipc
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	 $(ASAN_DIR)/fuzz-replay -m $(FUZZ_MUTATIONS) $(FUZZ_CORPUS)

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR) \
	             $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -m 0755 $(BIN_DIR)/kbdd      $(DESTDIR)$(BINDIR)/kbdd
	$(INSTALL) -m 0755 $(BIN_DIR)/kbctl     $(DESTDIR)$(BINDIR)/kbctl
	$(INSTALL) -m 0644 $(LIB_STATIC)        $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	$(INSTALL) -m 0755 $(LIB_SHARED_REAL)   $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_REAL)
	ln -sf $(LIB_SHARED_REAL) $(DESTDIR)$(LIBDIR)/$(LIB_SONAME)
	ln -sf $(LIB_SONAME)      $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	$(INSTALL) -m 0644 include/kbcore.h     $(DESTDIR)$(INCLUDEDIR)/kbcore.h
	@for f in packaging/udev/*.rules; do \
		[ -e "$$f" ] || continue; \
		$(INSTALL) -Dm0644 "$$f" "$(DESTDIR)$(UDEVDIR)/$$(basename "$$f")"; \
	done
	@for f in packaging/systemd/*.service; do \
		[ -e "$$f" ] || continue; \
		$(INSTALL) -Dm0644 "$$f" "$(DESTDIR)$(SYSTEMDUSERDIR)/$$(basename "$$f")"; \
		sed -i "s|^ExecStart=.*|ExecStart=$(BINDIR)/kbdd|" \
			"$(DESTDIR)$(SYSTEMDUSERDIR)/$$(basename "$$f")"; \
	done

clean:
	rm -rf $(BUILD_DIR) $(LIB_STATIC) $(LIB_SHARED) $(LIB_SONAME) $(LIB_SHARED_REAL)

# Fast, source-only architecture guard.  It fails the build before a forbidden
# dependency can be introduced.
check-arch:
	@echo "==> checking architecture boundaries"
	@if grep -RInE '#[[:space:]]*include[[:space:]]*[<"](gtk|gdk|X11|wayland|GL/|cairo)' \
		src/core include 2>/dev/null; then \
		echo "ERROR: GUI/X11 headers are forbidden in libkbcore" >&2; exit 1; \
	fi
	@if grep -RInE 'kbcore\.h|libkbcore' src/gui 2>/dev/null; then \
		echo "ERROR: a future GUI must not include or link libkbcore" >&2; exit 1; \
	fi
	@if grep -RInE '#[[:space:]]*include[[:space:]]*[<"](gtk|gdk|X11)' \
		src/daemon src/cli 2>/dev/null; then \
		echo "ERROR: X11/GTK headers are forbidden in kbdd and kbctl" >&2; exit 1; \
	fi

help:
	@echo "Targets: all core daemon cli test asan install clean"
	@echo "Layer rules: kbctl -> IPC -> kbdd -> libkbcore (no GUI in this tree)"
	@echo "Optional: make asan ASAN_RUNTIME_LIBDIR=/caminho/lib"

$(LIB_STATIC): $(CORE_OBJS)
	@echo "==> ar $@"
	@rm -f $@
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(LIB_SHARED_REAL): $(CORE_OBJS)
	@echo "==> link $@"
	$(CC) -shared -Wl,-soname,$(LIB_SONAME) -o $@ $(CORE_OBJS) $(LDFLAGS)
	ln -sf $@ $(LIB_SONAME)
	ln -sf $(LIB_SONAME) $(LIB_SHARED)

$(BIN_DIR)/kbdd: $(DAEMON_OBJS) $(LIB_STATIC)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(DAEMON_OBJS) $(LIB_STATIC) $(LDLIBS)

$(BIN_DIR)/kbctl: $(CLI_OBJS) $(LIB_STATIC)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(CLI_OBJS) $(LIB_STATIC) $(LDLIBS)

DEVTEST_OBJS := $(OBJ_DIR)/tests/device/test_device.o $(OBJ_DIR)/daemon/device.o \
                $(OBJ_DIR)/daemon/hidraw.o

$(BIN_DIR)/test-device: $(DEVTEST_OBJS) $(LIB_STATIC)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(DEVTEST_OBJS) $(LIB_STATIC) $(LDLIBS)

HANDLERTEST_OBJS := $(OBJ_DIR)/tests/test_handlers.o $(OBJ_DIR)/daemon/daemon.o \
                    $(OBJ_DIR)/daemon/config.o $(OBJ_DIR)/daemon/device.o \
                    $(OBJ_DIR)/daemon/hidraw.o $(OBJ_DIR)/daemon/ipc.o \
                    $(OBJ_DIR)/daemon/uinput.o $(OBJ_DIR)/daemon/op.o

$(BIN_DIR)/test-handlers: $(HANDLERTEST_OBJS) $(LIB_STATIC)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(HANDLERTEST_OBJS) $(LIB_STATIC) $(LDLIBS)

$(BIN_DIR)/test-cli: $(OBJ_DIR)/tests/test_cli_parse.o $(CLI_PARSER_OBJS)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(OBJ_DIR)/tests/test_cli_parse.o $(CLI_PARSER_OBJS) $(LDLIBS)

$(BIN_DIR)/test-ipc: $(OBJ_DIR)/tests/test_ipc.o $(IPC_OBJS) $(LIB_STATIC)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(OBJ_DIR)/tests/test_ipc.o $(IPC_OBJS) $(LIB_STATIC) $(LDLIBS)

$(BIN_DIR)/test-config: $(OBJ_DIR)/tests/test_config.o $(CONFIG_OBJS) $(LIB_STATIC)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(OBJ_DIR)/tests/test_config.o $(CONFIG_OBJS) $(LIB_STATIC) $(LDLIBS)

$(BIN_DIR)/fuzz-replay: $(FUZZ_OBJS) $(LIB_STATIC)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(FUZZ_OBJS) $(LIB_STATIC) $(LDLIBS)

$(ASAN_DIR)/fuzz-replay: $(ASAN_FUZZ_OBJS) $(ASAN_CORE_OBJS)
	@mkdir -p $(dir $@)
	@echo "==> link $@"
	$(CC) $(ASAN_LDFLAGS) -o $@ $(ASAN_FUZZ_OBJS) $(ASAN_CORE_OBJS) $(LDLIBS)

$(BIN_DIR)/test-core: $(TEST_OBJS) $(LIB_STATIC)
	@mkdir -p $(BIN_DIR)
	@echo "==> link $@"
	$(CC) $(LDFLAGS) $(EXE_LDFLAGS) -o $@ $(TEST_OBJS) $(LIB_STATIC) $(LDLIBS)

$(ASAN_DIR)/test-cli: $(ASAN_CLI_PARSER_OBJS)
	@mkdir -p $(dir $@)
	@echo "==> link $@"
	$(CC) $(ASAN_LDFLAGS) -o $@ $(ASAN_CLI_PARSER_OBJS) $(LDLIBS)


$(ASAN_DIR)/test-config: $(ASAN_CONFIG_OBJS) $(ASAN_CORE_OBJS)
	@mkdir -p $(dir $@)
	@echo "==> link $@"
	$(CC) $(ASAN_LDFLAGS) -o $@ $(ASAN_CONFIG_OBJS) $(ASAN_CORE_OBJS) $(LDLIBS)


$(ASAN_DIR)/test-ipc: $(ASAN_IPC_OBJS) $(ASAN_CORE_OBJS)
	@mkdir -p $(dir $@)
	@echo "==> link $@"
	$(CC) $(ASAN_LDFLAGS) -o $@ $(ASAN_IPC_OBJS) $(ASAN_CORE_OBJS) $(LDLIBS)


$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(ASAN_DIR)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(ASAN_FLAGS) -MMD -MP -c $< -o $@

$(ASAN_DIR)/obj/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(ASAN_FLAGS) -MMD -MP -c $< -o $@

ASAN_DEVTEST_OBJS := $(ASAN_DIR)/obj/tests/device/test_device.o \
                     $(ASAN_DIR)/obj/daemon/device.o $(ASAN_DIR)/obj/daemon/hidraw.o

ASAN_DAEMON_OBJS := $(ASAN_DIR)/obj/daemon/daemon.o $(ASAN_DIR)/obj/daemon/config.o \
                    $(ASAN_DIR)/obj/daemon/device.o $(ASAN_DIR)/obj/daemon/hidraw.o \
                    $(ASAN_DIR)/obj/daemon/ipc.o $(ASAN_DIR)/obj/daemon/uinput.o \
                    $(ASAN_DIR)/obj/daemon/op.o

$(ASAN_DIR)/test-device: $(ASAN_DEVTEST_OBJS) $(ASAN_CORE_OBJS)
	@mkdir -p $(dir $@)
	@echo "==> link $@"
	$(CC) $(ASAN_LDFLAGS) -o $@ $(ASAN_DEVTEST_OBJS) $(ASAN_CORE_OBJS) $(LDLIBS)

ASAN_HANDLERTEST_OBJS := $(ASAN_DIR)/obj/tests/test_handlers.o $(ASAN_DAEMON_OBJS)

$(ASAN_DIR)/test-handlers: $(ASAN_HANDLERTEST_OBJS) $(ASAN_CORE_OBJS)
	@mkdir -p $(dir $@)
	@echo "==> link $@"
	$(CC) $(ASAN_LDFLAGS) -o $@ $(ASAN_HANDLERTEST_OBJS) $(ASAN_CORE_OBJS) $(LDLIBS)

$(ASAN_DIR)/obj/tests/test_handlers.o: tests/test_handlers.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(ASAN_FLAGS) -MMD -MP -c $< -o $@

$(ASAN_DIR)/test-core: $(ASAN_TEST_OBJS) $(ASAN_CORE_OBJS)
	@mkdir -p $(dir $@)
	@echo "==> link $@"
	$(CC) $(ASAN_LDFLAGS) -o $@ $(ASAN_TEST_OBJS) $(ASAN_CORE_OBJS) $(LDLIBS)

-include $(DEPFILES)
