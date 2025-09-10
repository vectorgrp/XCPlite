# Makefile for building BPF program
# This should be run on a Linux system with BPF development tools installed

CLANG ?= clang
LLVM_STRIP ?= llvm-strip

# BPF program source and object
BPF_SRC = process_monitor.bpf.c
BPF_OBJ = process_monitor.bpf.o

# Kernel headers path (adjust as needed)
KERNEL_HEADERS ?= /usr/src/linux-headers-$(shell uname -r)
LIBBPF_HEADERS ?= /usr/include

# Detect architecture
ARCH := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')

# Compiler flags
CFLAGS := -O2 -g -Wall -Werror
BPF_CFLAGS := $(CFLAGS) -target bpf -D__TARGET_ARCH_$(ARCH) \
              -I$(LIBBPF_HEADERS) \
              -I$(KERNEL_HEADERS)/include \
              -I$(KERNEL_HEADERS)/include/uapi \
              -I$(KERNEL_HEADERS)/arch/$(ARCH)/include \
              -I$(KERNEL_HEADERS)/arch/$(ARCH)/include/uapi

.PHONY: all clean help

all: $(BPF_OBJ)

$(BPF_OBJ): $(BPF_SRC)
	@echo "Building BPF program for $(ARCH)..."
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@
	$(LLVM_STRIP) -g $@

clean:
	rm -f $(BPF_OBJ)

install: $(BPF_OBJ)
	@BUILD_DIR=$${BUILD_DIR:-../../../build}; \
	echo "Installing to: $$BUILD_DIR"; \
	mkdir -p "$$BUILD_DIR"; \
	cp $(BPF_OBJ) "$$BUILD_DIR/"; \
	echo "BPF object installed to $$BUILD_DIR/"

# Help target
help:
	@echo "Available targets:"
	@echo "  all     - Build the BPF program"
	@echo "  clean   - Remove built files"
	@echo "  install - Copy BPF object to build directory"
	@echo "  help    - Show this help"
	@echo ""
	@echo "Prerequisites (Ubuntu/Debian):"
	@echo "  sudo apt-get install clang llvm libbpf-dev linux-headers-\$$(uname -r)"
	@echo ""
	@echo "Prerequisites (RHEL/CentOS/Fedora):"
	@echo "  sudo dnf install clang llvm libbpf-devel kernel-headers"
