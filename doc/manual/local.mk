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

$(d)/%.1: $(d)/src/command-ref/%.md
	$(trace-gen) lowdown -sT man $^ -o $@

$(d)/%.8: $(d)/src/command-ref/%.md
	$(trace-gen) lowdown -sT man $^ -o $@

$(d)/nix.conf.5: $(d)/src/command-ref/conf-file.md
	$(trace-gen) lowdown -sT man $^ -o $@

$(d)/src/command-ref/nix.md: $(d)/nix.json $(d)/generate-manpage.jq
	jq -r -f doc/manual/generate-manpage.jq $< > $@

$(d)/src/command-ref/conf-file.md: $(d)/conf-file.json $(d)/generate-options.jq $(d)/src/command-ref/conf-file-prefix.md
	cat doc/manual/src/command-ref/conf-file-prefix.md > $@
	jq -r -f doc/manual/generate-options.jq $< >> $@

$(d)/nix.json: $(bindir)/nix
	$(trace-gen) $(bindir)/nix __dump-args > $@

$(d)/conf-file.json: $(bindir)/nix
	$(trace-gen) env -i NIX_CONF_DIR=/dummy HOME=/dummy $(bindir)/nix show-config --json --experimental-features nix-command > $@

$(d)/src/expressions/builtins.md: $(d)/builtins.json $(d)/generate-builtins.jq $(d)/src/expressions/builtins-prefix.md
	cat doc/manual/src/expressions/builtins-prefix.md > $@
	jq -r -f doc/manual/generate-builtins.jq $< >> $@

$(d)/builtins.json: $(bindir)/nix
	$(trace-gen) $(bindir)/nix __dump-builtins > $@

# Generate the HTML manual.
install: $(docdir)/manual/index.html

$(docdir)/manual/index.html: $(MANUAL_SRCS) $(d)/book.toml $(d)/custom.css $(d)/src/command-ref/nix.md $(d)/src/command-ref/conf-file.md $(d)/src/expressions/builtins.md
	$(trace-gen) mdbook build doc/manual -d $(docdir)/manual
	@cp doc/manual/highlight.pack.js $(docdir)/manual/highlight.js

endif
