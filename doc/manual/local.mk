ifeq ($(doc_generate),yes)

# Generate man pages.
man-pages := $(foreach n, \
  nix-env.1 nix-build.1 nix-shell.1 nix-store.1 nix-instantiate.1 \
  nix-collect-garbage.1 \
  nix-prefetch-url.1 nix-channel.1 \
  nix-hash.1 nix-copy-closure.1 \
  nix.conf.5 nix-daemon.8, \
  $(d)/$(n))

clean-files += $(d)/*.1 $(d)/*.5 $(d)/*.8

# Provide a dummy environment for nix, so that it will not access files outside the macOS sandbox.
dummy-env = env -i \
	HOME=/dummy \
	NIX_CONF_DIR=/dummy \
	NIX_SSL_CERT_FILE=/dummy/no-ca-bundle.crt \
	NIX_STATE_DIR=/dummy

nix-eval = $(dummy-env) $(bindir)/nix eval --experimental-features nix-command -I nix/corepkgs=corepkgs --store dummy:// --impure --raw

$(d)/%.1: $(d)/src/command-ref/%.md
	@printf "Title: %s\n\n" "$$(basename $@ .1)" > $^.tmp
	@cat $^ >> $^.tmp
	$(trace-gen) lowdown -sT man -M section=1 $^.tmp -o $@
	@rm $^.tmp

$(d)/%.8: $(d)/src/command-ref/%.md
	@printf "Title: %s\n\n" "$$(basename $@ .8)" > $^.tmp
	@cat $^ >> $^.tmp
	$(trace-gen) lowdown -sT man -M section=8 $^.tmp -o $@
	@rm $^.tmp

$(d)/nix.conf.5: $(d)/src/command-ref/conf-file.md
	@printf "Title: %s\n\n" "$$(basename $@ .5)" > $^.tmp
	@cat $^ >> $^.tmp
	$(trace-gen) lowdown -sT man -M section=5 $^.tmp -o $@
	@rm $^.tmp

$(d)/src/SUMMARY.md: $(d)/src/SUMMARY.md.in $(d)/src/command-ref/new-cli
	$(trace-gen) cat doc/manual/src/SUMMARY.md.in | while IFS= read line; do if [[ $$line = @manpages@ ]]; then cat doc/manual/src/command-ref/new-cli/SUMMARY.md; else echo "$$line"; fi; done > $@.tmp
	@mv $@.tmp $@

$(d)/src/command-ref/new-cli: $(d)/nix.json $(d)/generate-manpage.nix $(bindir)/nix
	@rm -rf $@
	$(trace-gen) $(nix-eval) --write-to $@ --expr 'import doc/manual/generate-manpage.nix (builtins.fromJSON (builtins.readFile $<))'

$(d)/src/command-ref/conf-file.md: $(d)/conf-file.json $(d)/generate-options.nix $(d)/src/command-ref/conf-file-prefix.md $(bindir)/nix
	@cat doc/manual/src/command-ref/conf-file-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) --expr 'import doc/manual/generate-options.nix (builtins.fromJSON (builtins.readFile $<))' >> $@.tmp
	@mv $@.tmp $@

$(d)/nix.json: $(bindir)/nix
	$(trace-gen) $(dummy-env) $(bindir)/nix __dump-args > $@.tmp
	@mv $@.tmp $@

$(d)/conf-file.json: $(bindir)/nix
	$(trace-gen) $(dummy-env) $(bindir)/nix show-config --json --experimental-features nix-command > $@.tmp
	@mv $@.tmp $@

$(d)/src/expressions/builtins.md: $(d)/builtins.json $(d)/generate-builtins.nix $(d)/src/expressions/builtins-prefix.md $(bindir)/nix
	@cat doc/manual/src/expressions/builtins-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) --expr 'import doc/manual/generate-builtins.nix (builtins.fromJSON (builtins.readFile $<))' >> $@.tmp
	@cat doc/manual/src/expressions/builtins-suffix.md >> $@.tmp
	@mv $@.tmp $@

$(d)/builtins.json: $(bindir)/nix
	$(trace-gen) $(dummy-env) NIX_PATH=nix/corepkgs=corepkgs $(bindir)/nix __dump-builtins > $@.tmp
	@mv $@.tmp $@

# Generate the HTML manual.
install: $(docdir)/manual/index.html

# Generate 'nix' manpages.
install: $(mandir)/man1/nix3-manpages
man: doc/manual/generated/man1/nix3-manpages
all: doc/manual/generated/man1/nix3-manpages

$(mandir)/man1/nix3-manpages: doc/manual/generated/man1/nix3-manpages
	@mkdir -p $$(dirname $@)
	$(trace-install) install -m 0644 $$(dirname $<)/* $$(dirname $@)

doc/manual/generated/man1/nix3-manpages: $(d)/src/command-ref/new-cli
	@mkdir -p $$(dirname $@)
	$(trace-gen) for i in doc/manual/src/command-ref/new-cli/*.md; do \
	  name=$$(basename $$i .md); \
	  tmpFile=$$(mktemp); \
	  if [[ $$name = SUMMARY ]]; then continue; fi; \
	  printf "Title: %s\n\n" "$$name" > $$tmpFile; \
	  cat $$i >> $$tmpFile; \
	  lowdown -sT man -M section=1 $$tmpFile -o $$(dirname $@)/$$name.1; \
	  rm $$tmpFile; \
	done
	touch $@

$(docdir)/manual/index.html: $(MANUAL_SRCS) $(d)/book.toml $(d)/custom.css $(d)/src/SUMMARY.md $(d)/src/command-ref/new-cli $(d)/src/command-ref/conf-file.md $(d)/src/expressions/builtins.md
	$(trace-gen) RUST_LOG=warn mdbook build doc/manual -d $(docdir)/manual

endif
