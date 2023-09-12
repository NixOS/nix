ifeq ($(doc_generate),yes)

MANUAL_SRCS := \
	$(call rwildcard, $(d)/src, *.md) \
	$(call rwildcard, $(d)/src, */*.md)

man-pages := $(foreach n, \
	nix-env.1 nix-store.1 \
	nix-build.1 nix-shell.1 nix-instantiate.1 \
	nix-collect-garbage.1 \
	nix-prefetch-url.1 nix-channel.1 \
	nix-hash.1 nix-copy-closure.1 \
	nix.conf.5 nix-daemon.8 \
	nix-profiles.5 \
, $(d)/$(n))

# man pages for subcommands
# convert from `$(d)/src/command-ref/nix-{1}/{2}.md` to `$(d)/nix-{1}-{2}.1`
# FIXME: unify with how nix3-cli man pages are generated
man-pages += $(foreach subcommand, \
	$(filter-out %opt-common.md %env-common.md, $(wildcard $(d)/src/command-ref/nix-*/*.md)), \
	$(d)/$(subst /,-,$(subst $(d)/src/command-ref/,,$(subst .md,.1,$(subcommand)))))

clean-files += $(d)/*.1 $(d)/*.5 $(d)/*.8

# Provide a dummy environment for nix, so that it will not access files outside the macOS sandbox.
# Set cores to 0 because otherwise nix show-config resolves the cores based on the current machine
dummy-env = env -i \
	HOME=/dummy \
	NIX_CONF_DIR=/dummy \
	NIX_SSL_CERT_FILE=/dummy/no-ca-bundle.crt \
	NIX_STATE_DIR=/dummy \
	NIX_CONFIG='cores = 0'

nix-eval = $(dummy-env) $(bindir)/nix eval --experimental-features nix-command -I nix/corepkgs=corepkgs --store dummy:// --impure --raw

# re-implement mdBook's include directive to make it usable for terminal output and for proper @docroot@ substitution
define process-includes
	while read -r line; do \
		set -euo pipefail; \
		filename="$$(dirname $(1))/$$(sed 's/{{#include \(.*\)}}/\1/'<<< $$line)"; \
		test -f "$$filename" || ( echo "#include-d file '$$filename' does not exist." >&2; exit 1; ); \
		matchline="$$(sed 's|/|\\/|g' <<< $$line)"; \
		sed -i "/$$matchline/r $$filename" $(2); \
		sed -i "s/$$matchline//" $(2); \
	done < <(grep '{{#include' $(1))
endef

$(d)/nix-env-%.1: $(d)/src/command-ref/nix-env/%.md
	@printf "Title: %s\n\n" "$(subst nix-env-,nix-env --,$$(basename "$@" .1))" > $^.tmp
	$(render-subcommand)

$(d)/nix-store-%.1: $(d)/src/command-ref/nix-store/%.md
	@printf -- 'Title: %s\n\n' "$(subst nix-store-,nix-store --,$$(basename "$@" .1))" > $^.tmp
	$(render-subcommand)

# FIXME: there surely is some more deduplication to be achieved here with even darker Make magic
define render-subcommand
  @cat $^ >> $^.tmp
	@$(call process-includes,$^,$^.tmp)
	$(trace-gen) lowdown -sT man --nroff-nolinks -M section=1 $^.tmp -o $@
	@# fix up `lowdown`'s automatic escaping of `--`
	@# https://github.com/kristapsdz/lowdown/blob/edca6ce6d5336efb147321a43c47a698de41bb7c/entity.c#L202
	@sed -i 's/\e\[u2013\]/--/' $@
	@rm $^.tmp
endef


$(d)/%.1: $(d)/src/command-ref/%.md
	@printf "Title: %s\n\n" "$$(basename $@ .1)" > $^.tmp
	@cat $^ >> $^.tmp
	@$(call process-includes,$^,$^.tmp)
	$(trace-gen) lowdown -sT man --nroff-nolinks -M section=1 $^.tmp -o $@
	@rm $^.tmp

$(d)/%.8: $(d)/src/command-ref/%.md
	@printf "Title: %s\n\n" "$$(basename $@ .8)" > $^.tmp
	@cat $^ >> $^.tmp
	$(trace-gen) lowdown -sT man --nroff-nolinks -M section=8 $^.tmp -o $@
	@rm $^.tmp

$(d)/nix.conf.5: $(d)/src/command-ref/conf-file.md
	@printf "Title: %s\n\n" "$$(basename $@ .5)" > $^.tmp
	@cat $^ >> $^.tmp
	@$(call process-includes,$^,$^.tmp)
	$(trace-gen) lowdown -sT man --nroff-nolinks -M section=5 $^.tmp -o $@
	@rm $^.tmp

$(d)/nix-profiles.5: $(d)/src/command-ref/files/profiles.md
	@printf "Title: %s\n\n" "$$(basename $@ .5)" > $^.tmp
	@cat $^ >> $^.tmp
	$(trace-gen) lowdown -sT man --nroff-nolinks -M section=5 $^.tmp -o $@
	@rm $^.tmp

$(d)/src/SUMMARY.md: $(d)/src/SUMMARY.md.in $(d)/src/command-ref/new-cli $(d)/src/contributing/experimental-feature-descriptions.md
	@cp $< $@
	@$(call process-includes,$@,$@)

$(d)/src/command-ref/new-cli: $(d)/nix.json $(d)/utils.nix $(d)/generate-manpage.nix $(bindir)/nix
	@rm -rf $@ $@.tmp
	$(trace-gen) $(nix-eval) --write-to $@.tmp --expr 'import doc/manual/generate-manpage.nix (builtins.readFile $<)'
	@mv $@.tmp $@

$(d)/src/command-ref/conf-file.md: $(d)/conf-file.json $(d)/utils.nix $(d)/src/command-ref/conf-file-prefix.md $(d)/src/command-ref/experimental-features-shortlist.md $(bindir)/nix
	@cat doc/manual/src/command-ref/conf-file-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) --expr '(import doc/manual/utils.nix).showSettings { useAnchors = true; } (builtins.fromJSON (builtins.readFile $<))' >> $@.tmp;
	@mv $@.tmp $@

$(d)/nix.json: $(bindir)/nix
	$(trace-gen) $(dummy-env) $(bindir)/nix __dump-cli > $@.tmp
	@mv $@.tmp $@

$(d)/conf-file.json: $(bindir)/nix
	$(trace-gen) $(dummy-env) $(bindir)/nix show-config --json --experimental-features nix-command > $@.tmp
	@mv $@.tmp $@

$(d)/src/contributing/experimental-feature-descriptions.md: $(d)/xp-features.json $(d)/utils.nix $(d)/generate-xp-features.nix $(bindir)/nix
	@rm -rf $@ $@.tmp
	$(trace-gen) $(nix-eval) --write-to $@.tmp --expr 'import doc/manual/generate-xp-features.nix (builtins.fromJSON (builtins.readFile $<))'
	@mv $@.tmp $@

$(d)/src/command-ref/experimental-features-shortlist.md: $(d)/xp-features.json $(d)/utils.nix $(d)/generate-xp-features-shortlist.nix $(bindir)/nix
	@rm -rf $@ $@.tmp
	$(trace-gen) $(nix-eval) --write-to $@.tmp --expr 'import doc/manual/generate-xp-features-shortlist.nix (builtins.fromJSON (builtins.readFile $<))'
	@mv $@.tmp $@

$(d)/xp-features.json: $(bindir)/nix
	$(trace-gen) $(dummy-env) NIX_PATH=nix/corepkgs=corepkgs $(bindir)/nix __dump-xp-features > $@.tmp
	@mv $@.tmp $@

$(d)/src/language/builtins.md: $(d)/language.json $(d)/generate-builtins.nix $(d)/src/language/builtins-prefix.md $(bindir)/nix
	@cat doc/manual/src/language/builtins-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) --expr 'import doc/manual/generate-builtins.nix (builtins.fromJSON (builtins.readFile $<)).builtins' >> $@.tmp;
	@cat doc/manual/src/language/builtins-suffix.md >> $@.tmp
	@mv $@.tmp $@

$(d)/src/language/builtin-constants.md: $(d)/language.json $(d)/generate-builtin-constants.nix $(d)/src/language/builtin-constants-prefix.md $(bindir)/nix
	@cat doc/manual/src/language/builtin-constants-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) --expr 'import doc/manual/generate-builtin-constants.nix (builtins.fromJSON (builtins.readFile $<)).constants' >> $@.tmp;
	@cat doc/manual/src/language/builtin-constants-suffix.md >> $@.tmp
	@mv $@.tmp $@

$(d)/language.json: $(bindir)/nix
	$(trace-gen) $(dummy-env) NIX_PATH=nix/corepkgs=corepkgs $(bindir)/nix __dump-language > $@.tmp
	@mv $@.tmp $@

# Generate the HTML manual.
.PHONY: manual-html
manual-html: $(docdir)/manual/index.html
install: $(docdir)/manual/index.html

# Generate 'nix' manpages.
install: $(mandir)/man1/nix3-manpages
man: doc/manual/generated/man1/nix3-manpages
all: doc/manual/generated/man1/nix3-manpages

# FIXME: unify with how the other man pages are generated.
# this one works differently and does not use any of the amenities provided by `/mk/lib.mk`.
$(mandir)/man1/nix3-manpages: doc/manual/generated/man1/nix3-manpages
	@mkdir -p $(DESTDIR)$$(dirname $@)
	$(trace-install) install -m 0644 $$(dirname $<)/* $(DESTDIR)$$(dirname $@)

doc/manual/generated/man1/nix3-manpages: $(d)/src/command-ref/new-cli
	@mkdir -p $(DESTDIR)$$(dirname $@)
	$(trace-gen) for i in doc/manual/src/command-ref/new-cli/*.md; do \
		name=$$(basename $$i .md); \
		tmpFile=$$(mktemp); \
		if [[ $$name = SUMMARY ]]; then continue; fi; \
		printf "Title: %s\n\n" "$$name" > $$tmpFile; \
		cat $$i >> $$tmpFile; \
		lowdown -sT man --nroff-nolinks -M section=1 $$tmpFile -o $(DESTDIR)$$(dirname $@)/$$name.1; \
		rm $$tmpFile; \
	done
	@touch $@

$(docdir)/manual/index.html: $(MANUAL_SRCS) $(d)/book.toml $(d)/anchors.jq $(d)/custom.css $(d)/src/SUMMARY.md $(d)/src/command-ref/new-cli $(d)/src/contributing/experimental-feature-descriptions.md $(d)/src/command-ref/conf-file.md $(d)/src/language/builtins.md $(d)/src/language/builtin-constants.md
	$(trace-gen) \
		tmp="$$(mktemp -d)"; \
		cp -r doc/manual "$$tmp"; \
		find "$$tmp" -name '*.md' | while read -r file; do \
			$(call process-includes,$$file,$$file); \
		done; \
		find "$$tmp" -name '*.md' | while read -r file; do \
			docroot="$$(realpath --relative-to="$$(dirname "$$file")" $$tmp/manual/src)"; \
			sed -i "s,@docroot@,$$docroot,g" "$$file"; \
		done; \
		set -euo pipefail; \
		RUST_LOG=warn mdbook build "$$tmp/manual" -d $(DESTDIR)$(docdir)/manual.tmp 2>&1 \
			| { grep -Fv "because fragment resolution isn't implemented" || :; }; \
		rm -rf "$$tmp/manual"
	@rm -rf $(DESTDIR)$(docdir)/manual
	@mv $(DESTDIR)$(docdir)/manual.tmp/html $(DESTDIR)$(docdir)/manual
	@rm -rf $(DESTDIR)$(docdir)/manual.tmp

endif
