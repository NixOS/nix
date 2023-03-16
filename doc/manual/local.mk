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
, $(d)/$(n))

clean-files += $(d)/*.1 $(d)/*.5 $(d)/*.8

# Provide a dummy environment for nix, so that it will not access files outside the macOS sandbox.
# Set cores to 0 because otherwise nix show-config resolves the cores based on the current machine
dummy-env = env -i \
	HOME=/dummy \
	NIX_CONF_DIR=/dummy \
	NIX_SSL_CERT_FILE=/dummy/no-ca-bundle.crt \
	NIX_STATE_DIR=/dummy \
	NIX_CONFIG='cores = 0'

nix-eval = $(dummy-env) src/nix/nix eval --experimental-features nix-command -I nix/corepkgs=corepkgs --store dummy:// --impure --raw

# re-implement mdBook's include directive to make it usable for terminal output and for proper @docroot@ substitution
define process-includes
	while read -r line; do \
		filename=$$(sed 's/{{#include \(.*\)}}/\1/'<<< $$line); \
		matchline=$$(sed 's|/|\\/|g' <<< $$line); \
		sed -i "/$$matchline/r $$(dirname $(2))/$$filename" $(2); \
		sed -i "s/$$matchline//" $(2); \
	done < <(grep '{{#include' $(1))
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
	$(trace-gen) lowdown -sT man --nroff-nolinks -M section=5 $^.tmp -o $@
	@rm $^.tmp

$(d)/src/SUMMARY.md: $(d)/src/SUMMARY.md.in $(d)/src/command-ref/new-cli
	@cp $< $@
	@$(call process-includes,$@,$@)

$(d)/src/command-ref/new-cli: $(d)/nix.json $(d)/generate-manpage.nix src/nix/nix
	@rm -rf $@
	$(trace-gen) $(nix-eval) --write-to $@.tmp --expr 'import doc/manual/generate-manpage.nix { toplevel = builtins.readFile $<; }'
	@mv $@.tmp $@

$(d)/src/command-ref/conf-file.md: $(d)/conf-file.json $(d)/generate-options.nix $(d)/src/command-ref/conf-file-prefix.md src/nix/nix
	@cat doc/manual/src/command-ref/conf-file-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) --expr 'import doc/manual/generate-options.nix (builtins.fromJSON (builtins.readFile $<))' >> $@.tmp;
	@mv $@.tmp $@

$(d)/nix.json: src/nix/nix
	$(trace-gen) $(dummy-env) src/nix/nix __dump-args > $@.tmp
	@mv $@.tmp $@

$(d)/conf-file.json: src/nix/nix
	$(trace-gen) $(dummy-env) src/nix/nix show-config --json --experimental-features nix-command > $@.tmp
	@mv $@.tmp $@

$(d)/src/language/builtins.md: $(d)/builtins.json $(d)/generate-builtins.nix $(d)/src/language/builtins-prefix.md src/nix/nix
	@cat doc/manual/src/language/builtins-prefix.md > $@.tmp
	$(trace-gen) $(nix-eval) --expr 'import doc/manual/generate-builtins.nix (builtins.fromJSON (builtins.readFile $<))' >> $@.tmp;
	@cat doc/manual/src/language/builtins-suffix.md >> $@.tmp
	@mv $@.tmp $@

$(d)/builtins.json: src/nix/nix
	$(trace-gen) $(dummy-env) NIX_PATH=nix/corepkgs=corepkgs src/nix/nix __dump-builtins > $@.tmp
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

$(docdir)/manual/index.html: $(MANUAL_SRCS) $(d)/book.toml $(d)/anchors.jq $(d)/custom.css $(d)/src/SUMMARY.md $(d)/src/command-ref/new-cli $(d)/src/command-ref/conf-file.md $(d)/src/language/builtins.md
	$(trace-gen) \
		tmp="$$(mktemp -d)"; \
		cp -r doc/manual "$$tmp"; \
		find "$$tmp" -name '*.md' | while read -r file; do \
			$(call process-includes,$$file,$$file); \
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
