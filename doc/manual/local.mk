ifeq ($(doc_generate),yes)

MANUAL_SRCS := $(call rwildcard, $(d)/src, *.md)

# Generate man pages.
man-pages := $(foreach n, \
  nix-env.1 nix-build.1 nix-shell.1 nix-store.1 nix-instantiate.1 nix.1 \
  nix-collect-garbage.1 \
  nix-prefetch-url.1 nix-channel.1 \
  nix-hash.1 nix-copy-closure.1 \
  nix.conf.5 nix-daemon.8, \
  $(d)/$(n))

clean-files += $(d)/*.1 $(d)/*.5 $(d)/*.8

dist-files += $(man-pages)

nix-eval = $(bindir)/nix eval --experimental-features nix-command -I nix/corepkgs=corepkgs --store dummy:// --impure --raw --expr

$(d)/%.1: $(d)/src/command-ref/%.md
	$(trace-gen) lowdown -sT man $^ -o $@

$(d)/%.8: $(d)/src/command-ref/%.md
	$(trace-gen) lowdown -sT man $^ -o $@

$(d)/nix.conf.5: $(d)/src/command-ref/conf-file.md
	$(trace-gen) lowdown -sT man $^ -o $@

$(d)/src/command-ref/nix.md: $(d)/nix.json $(d)/generate-manpage.nix $(bindir)/nix
	$(trace-gen) $(nix-eval) 'import doc/manual/generate-manpage.nix (builtins.fromJSON (builtins.readFile $<))' > $@.tmp
	@mv $@.tmp $@

$(d)/src/command-ref/conf-file.md: $(d)/conf-file.json $(d)/generate-options.nix $(d)/src/command-ref/conf-file-prefix.md $(bindir)/nix
	@cat doc/manual/src/command-ref/conf-file-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) 'import doc/manual/generate-options.nix (builtins.fromJSON (builtins.readFile $<))' >> $@.tmp
	@mv $@.tmp $@

$(d)/nix.json: $(bindir)/nix
	$(trace-gen) $(bindir)/nix __dump-args > $@.tmp
	@mv $@.tmp $@

$(d)/conf-file.json: $(bindir)/nix
	$(trace-gen) env -i NIX_CONF_DIR=/dummy HOME=/dummy $(bindir)/nix show-config --json --experimental-features nix-command > $@.tmp
	@mv $@.tmp $@

$(d)/src/expressions/builtins.md: $(d)/builtins.json $(d)/generate-builtins.nix $(d)/src/expressions/builtins-prefix.md $(bindir)/nix
	@cat doc/manual/src/expressions/builtins-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) 'import doc/manual/generate-builtins.nix (builtins.fromJSON (builtins.readFile $<))' >> $@.tmp
	@mv $@.tmp $@

$(d)/builtins.json: $(bindir)/nix
	$(trace-gen) NIX_PATH=nix/corepkgs=corepkgs $(bindir)/nix __dump-builtins > $@.tmp
	mv $@.tmp $@

# Generate the HTML manual.
install: $(docdir)/manual/index.html

$(docdir)/manual/index.html: $(MANUAL_SRCS) $(d)/book.toml $(d)/custom.css $(d)/src/command-ref/nix.md $(d)/src/command-ref/conf-file.md $(d)/src/expressions/builtins.md
	$(trace-gen) mdbook build doc/manual -d $(docdir)/manual
	@cp doc/manual/highlight.pack.js $(docdir)/manual/highlight.js

endif
