
ifeq ($(doc_generate),yes)

NONET ?= 0
NONET_FLAGS_1 = --nonet
NONET_FLAGS = $(NONET_FLAGS_$(NONET))

XSLTPROC = $(xsltproc) $(NONET_flag) $(xmlflags) \
  --param section.autolabel 1 \
  --param section.label.includes.component.label 1 \
  --param html.stylesheet \'style.css\' \
  --param xref.with.number.and.title 1 \
  --param toc.section.depth 3 \
  --param admon.style \'\' \
  --param callout.graphics.extension \'.gif\' \
  --param contrib.inline.enabled 0 \
  --stringparam generate.toc "book toc" \
  --param keep.relative.image.uris 0

ifeq ($(NONET),0)
    docbookxsl = /usr/share/xml/docbook/stylesheet/docbook-xsl
    docbookrng = $(docbookxsl)/slides/schema/relaxng/docbook.rng
    profile_xsl= $(docbookxsl)/profiling/profile.xsl
    docbook_xsl= $(docbookxsl)/manpages/profile-docbook.xsl
else
    docbookrng = "http://docbook.org/xml/5.0/rng/docbook.rng"
    docbookxsl = "http://docbook.sourceforge.net/release/xsl-ns/current"
    profile_xsl= $(docbookxsl)/profiling/profile.xsl
    docbook_xsl= $(docbookxsl)/manpages/docbook.xsl
endif

MANUAL_SRCS := $(call rwildcard, $(d), *.xml)

# save $(d) for use *inside* rules
doc_manual_DIR := $(d)
doc_manual_OUT := $(buildprefix)$(reldir)

# Do XInclude processing / RelaxNG validation
$(doc_manual_OUT)/manual.xmli: $(d)/manual.xml $(MANUAL_SRCS) $(d)/version.txt
	@echo reldir=$(doc_manual_OUT)
	@mkdir -p $(doc_manual_OUT)
	$(trace-gen) (cd $(doc_manual_DIR) && $(xmllint) $(NONET_FLAGS) --xinclude manual.xml) > $@.tmp
	@mv $@.tmp $@

$(d)/version.txt:
	$(trace-gen) echo -n $(PACKAGE_VERSION) > $@

# Note: RelaxNG validation requires xmllint >= 2.7.4.
$(doc_manual_OUT)/manual.is-valid: $(doc_manual_OUT)/manual.xmli
	$(trace-gen) $(XSLTPROC) --novalid --stringparam profile.condition manual \
	  $(docbookxsl)/profiling/profile.xsl $< 2> /dev/null | \
	  $(xmllint) $(NONET_FLAGS) --noout --relaxng $(docbookrng) -
	@touch $@

clean-files += $(doc_manual_OUT)/manual.xmli version.txt $(doc_manual_OUT)/manual.is-valid

dist-files += $(doc_manual_OUT)/manual.xmli version.txt $(doc_manual_OUT)/manual.is-valid

# Generate man pages.
man-pages := $(foreach n, \
  nix-env.1 nix-build.1 nix-shell.1 nix-store.1 nix-instantiate.1 \
  nix-collect-garbage.1 \
  nix-prefetch-url.1 nix-channel.1 \
  nix-hash.1 nix-copy-closure.1 \
  nix.conf.5 nix-daemon.8, \
  $(doc_manual_OUT)/$(n))

$(firstword $(man-pages)): $(doc_manual_OUT)/manual.xmli $(doc_manual_OUT)/manual.is-valid
	@mkdir -p $(doc_manual_OUT)
	$(trace-gen) $(XSLTPROC) --novalid --stringparam profile.condition manpage \
	  $(docbookxsl)/profiling/profile.xsl $< 2> /dev/null | \
	  (cd $(doc_manual_OUT) && $(XSLTPROC) $(docbookxsl)/manpages/docbook.xsl -)

$(wordlist 2, $(words $(man-pages)), $(man-pages)): $(firstword $(man-pages))

clean-files += $(doc_manual_OUT)/*.1 $(doc_manual_OUT)/*.5 $(doc_manual_OUT)/*.8

dist-files += $(man-pages)


# Generate the HTML manual.
$(doc_manual_OUT)/manual.html: $(d)/manual.xml $(MANUAL_SRCS) $(doc_manual_OUT)/manual.is-valid
	$(trace-gen) $(XSLTPROC) --xinclude --stringparam profile.condition manual \
	  $(docbookxsl)/profiling/profile.xsl $< | \
	  $(XSLTPROC) --output $@ $(docbookxsl)/xhtml/docbook.xsl -

$(foreach file, $(doc_manual_OUT)/manual.html $(d)/style.css, $(eval $(call install-data-in, $(file), $(docdir)/manual)))

$(foreach file, $(wildcard $(d)/figures/*.png), $(eval $(call install-data-in, $(file), $(docdir)/manual/figures)))

$(foreach file, $(wildcard $(d)/images/callouts/*.gif), $(eval $(call install-data-in, $(file), $(docdir)/manual/images/callouts)))

$(eval $(call install-symlink, $(doc_manual_OUT)/manual.html, $(docdir)/manual/index.html))


all: $(doc_manual_OUT)/manual.html



clean-files += $(doc_manual_OUT)/manual.html

dist-files += $(doc_manual_OUT)/manual.html


endif
