ifeq ($(OPTIMIZE), 1)
  RUST_MODE = --release
  RUST_DIR = release
else
  RUST_MODE =
  RUST_DIR = debug
endif

libnixrust_PATH := $(d)/target/$(RUST_DIR)/libnixrust.a
libnixrust_INSTALL_PATH := $(libnixrust_PATH)
libnixrust_LDFLAGS_USE := -L$(d)/target/$(RUST_DIR) -lnixrust -ldl
libnixrust_LDFLAGS_USE_INSTALLED := $(libnixrust_LDFLAGS_USE)

$(d)/target/$(RUST_DIR)/libnixrust.a: $(wildcard $(d)/src/*.rs) $(d)/Cargo.toml
	$(trace-gen) cd nix-rust && CARGO_HOME=$$(if [[ -d vendor ]]; then echo vendor; fi) \
	cargo build $(RUST_MODE) $$(if [[ -d vendor ]]; then echo --offline; fi) \
	&& touch target/$(RUST_DIR)/libnixrust.a

dist-files += $(d)/vendor
