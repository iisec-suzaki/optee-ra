CC      ?= $(CROSS_COMPILE)gcc
LD      ?= $(CROSS_COMPILE)ld
AR      ?= $(CROSS_COMPILE)ar
NM      ?= $(CROSS_COMPILE)nm
OBJCOPY ?= $(CROSS_COMPILE)objcopy
OBJDUMP ?= $(CROSS_COMPILE)objdump
READELF ?= $(CROSS_COMPILE)readelf

OBJS = main.o client.o

CFLAGS += -Wall -DHOST_BUILD -I../ta/include -I$(TEEC_EXPORT)/include -I./include

RUST_APICLIENT_DIR ?= $(CURDIR)/rust-ffi/coserv-rs
RUST_APICLIENT_TARGET ?= aarch64-unknown-linux-gnu

ifeq ($(wildcard $(RUST_APICLIENT_DIR)/Cargo.toml),)
ifneq ($(wildcard /rust-apiclient/Cargo.toml),)
RUST_APICLIENT_DIR := /rust-apiclient
endif
endif

RUST_APICLIENT_LIB ?= $(RUST_APICLIENT_DIR)/target/$(RUST_APICLIENT_TARGET)/release/libveraison_apiclient_ffi.a

#Add/link other required libraries here
LDADD += -lteec -L$(TEEC_EXPORT)/lib $(RUST_APICLIENT_LIB) -lssl -lcrypto -lm

BINARY = optee_remote_attestation

.PHONY: all
all: $(BINARY)

$(BINARY): $(OBJS)
	@test -f "$(RUST_APICLIENT_LIB)" || { \
		echo "Missing rust-apiclient FFI library at $(RUST_APICLIENT_LIB)."; \
		echo "Set RUST_APICLIENT_DIR or run rust-ffi/fetch-rust-apiclient.sh and build it."; \
		exit 1; \
	}
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDADD)

.PHONY: clean
clean:
	rm -f $(OBJS) $(BINARY)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
