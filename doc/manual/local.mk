
ifeq ($(doc_generate),yes)

XSLTPROC = $(xsltproc) --nonet $(xmlflags) \
  --param section.autolabel 1 \
  --param section.label.includes.component.label 1 \
  --param xref.with.number.and.title 1 \
  --param toc.section.depth 3 \
  --param admon.style \'\' \
  --param callout.graphics.extension \'.gif\' \
  --param contrib.inline.enabled 0 \
  --stringparam generate.toc "book toc" \
  --param keep.relative.image.uris 0

docbookxsl = http://docbook.sourceforge.net/release/xsl-ns/current
docbookrng = http://docbook.org/xml/5.0/rng/docbook.rng

MANUAL_SRCS := $(call rwildcard, $(d), *.xml)


# Do XInclude processing / RelaxNG validation
$(d)/manual.xmli: $(d)/manual.xml $(MANUAL_SRCS) $(d)/version.txt
	$(trace-gen) $(xmllint) --nonet --xinclude $< -o $@.tmp
	@mv $@.tmp $@

$(d)/version.txt:
	$(trace-gen) echo -n $(PACKAGE_VERSION) > $@

# Note: RelaxNG validation requires xmllint >= 2.7.4.
$(d)/manual.is-valid: $(d)/manual.xmli
	$(trace-gen) $(XSLTPROC) --novalid --stringparam profile.condition manual \
	  $(docbookxsl)/profiling/profile.xsl $< 2> /dev/null | \
	  $(xmllint) --nonet --noout --relaxng $(docbookrng) -
	@touch $@

clean-files += $(d)/manual.xmli $(d)/version.txt $(d)/manual.is-valid

dist-files += $(d)/manual.xmli $(d)/version.txt $(d)/manual.is-valid


# Generate man pages.
man-pages := $(foreach n, \
  nix-env.1 nix-build.1 nix-shell.1 nix-store.1 nix-instantiate.1 \
  nix-collect-garbage.1 \
  nix-prefetch-url.1 nix-channel.1 \
  nix-hash.1 nix-copy-closure.1 \
  nix.conf.5 nix-daemon.8, \
  $(d)/$(n))

$(firstword $(man-pages)): $(d)/manual.xmli $(d)/manual.is-valid
	$(trace-gen) $(XSLTPROC) --novalid --stringparam profile.condition manpage \
	  $(docbookxsl)/profiling/profile.xsl $< 2> /dev/null | \
	  (cd doc/manual && $(XSLTPROC) $(docbookxsl)/manpages/docbook.xsl -)

$(wordlist 2, $(words $(man-pages)), $(man-pages)): $(firstword $(man-pages))

clean-files += $(d)/*.1 $(d)/*.5 $(d)/*.8

dist-files += $(man-pages)


# Generate the HTML manual.
$(d)/manual.html: $(d)/manual.xml $(MANUAL_SRCS) $(d)/manual.is-valid
	$(trace-gen) $(XSLTPROC) --xinclude --stringparam profile.condition manual \
	  $(docbookxsl)/profiling/profile.xsl $< | \
	  $(XSLTPROC) --output $@ $(docbookxsl)/xhtml/docbook.xsl -

$(foreach file, $(d)/manual.html, $(eval $(call install-data-in, $(file), $(docdir)/manual)))

$(foreach file, $(wildcard $(d)/figures/*.png), $(eval $(call install-data-in, $(file), $(docdir)/manual/figures)))

$(foreach file, $(wildcard $(d)/images/callouts/*.gif), $(eval $(call install-data-in, $(file), $(docdir)/manual/images/callouts)))

$(eval $(call install-symlink, manual.html, $(docdir)/manual/index.html))


all: $(d)/manual.html



clean-files += $(d)/manual.html

dist-files += $(d)/manual.html


endif
