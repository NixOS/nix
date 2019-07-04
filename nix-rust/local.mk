libnixrust_PATH := $(d)/target/release/libnixrust.a
libnixrust_INSTALL_PATH := $(libnixrust_PATH)
libnixrust_LDFLAGS_USE := -L$(d)/target/release -lnixrust -ldl
libnixrust_LDFLAGS_USE_INSTALLED := $(libnixrust_LDFLAGS_USE)

$(d)/target/release/libnixrust.a: $(wildcard $(d)/src/*.rs) $(d)/Cargo.toml
	$(trace-gen) cd nix-rust && CARGO_HOME=$$(if [[ -d vendor ]]; then echo vendor; fi) cargo build --release -Z offline && touch target/release/libnixrust.a

dist-files += $(d)/vendor
