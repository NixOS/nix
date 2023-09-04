ifeq ($(OPTIMIZE), 1)
  RUST_MODE = --release
  RUST_DIR = release
else
  RUST_MODE =
  RUST_DIR = debug
endif

libnixrust_PATH := $(d)/target/$(RUST_DIR)/libnixrust.$(SO_EXT)
libnixrust_INSTALL_PATH := $(libdir)/libnixrust.$(SO_EXT)
libnixrust_LDFLAGS_USE := -L$(d)/target/$(RUST_DIR) -lnixrust
libnixrust_LDFLAGS_USE_INSTALLED := -L$(libdir) -lnixrust

ifdef HOST_LINUX
libnixrust_LDFLAGS_USE += -ldl
libnixrust_LDFLAGS_USE_INSTALLED += -ldl
endif

ifdef HOST_DARWIN
libnixrust_BUILD_FLAGS = NIX_LDFLAGS="-undefined dynamic_lookup"
else
libnixrust_LDFLAGS_USE += -Wl,-rpath,$(abspath $(d)/target/$(RUST_DIR))
libnixrust_LDFLAGS_USE_INSTALLED += -Wl,-rpath,$(libdir)
endif

$(libnixrust_PATH): $(call rwildcard, $(d)/src, *.rs) $(d)/Cargo.toml
	$(trace-gen) cd nix-rust && CARGO_HOME=$$(if [[ -d vendor ]]; then echo vendor; fi) \
	$(libnixrust_BUILD_FLAGS) \
	  cargo build $(RUST_MODE) $$(if [[ -d vendor ]]; then echo --offline; fi) \
	&& touch target/$(RUST_DIR)/libnixrust.$(SO_EXT)

$(libnixrust_INSTALL_PATH): $(libnixrust_PATH)
	$(target-gen) cp $^ $@
ifdef HOST_DARWIN
	install_name_tool -id $@ $@
endif

clean: clean-rust

clean-rust:
	$(suppress) rm -rfv nix-rust/target

ifndef HOST_DARWIN
check: rust-tests

rust-tests:
	$(trace-test) cd nix-rust && CARGO_HOME=$$(if [[ -d vendor ]]; then echo vendor; fi) cargo test --release $$(if [[ -d vendor ]]; then echo --offline; fi)
endif
