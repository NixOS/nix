ifeq ($(doc_generate),yes)

MANUAL_SRCS := $(call rwildcard, $(d)/src, *.md)

#$(d)/version.txt:
#	$(trace-gen) echo -n $(PACKAGE_VERSION) > $@

clean-files += $(d)/version.txt

dist-files += $(d)/version.txt


# Generate man pages.
man-pages := $(foreach n, \
  nix-copy-closure.1, \
  $(d)/$(n))
#  nix-env.1 nix-build.1 nix-shell.1 nix-store.1 nix-instantiate.1 \
#  nix-collect-garbage.1, \
#  nix-prefetch-url.1 nix-channel.1 \
#  nix-hash.1 nix-copy-closure.1 \
#  nix.conf.5 nix-daemon.8, \

clean-files += $(d)/*.1 $(d)/*.5 $(d)/*.8

dist-files += $(man-pages)

$(d)/nix-copy-closure.1: $(d)/src/command-ref/nix-copy-closure.md
	$(trace-gen) lowdown -sT man $^ -o $@

# Generate the HTML manual.
install: $(docdir)/manual/index.html

$(docdir)/manual/index.html: $(MANUAL_SRCS)
	$(trace-gen) mdbook build doc/manual -d $(docdir)/manual


endif
