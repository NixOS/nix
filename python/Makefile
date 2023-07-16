# This Makefile is only used for development of the Python bindings, it is not
# used in the Nix build.
# The reason this exists is to make it easier to develop the Python bindings in
# tandem with the main Nix.
# The default `make` (defaults to `make build`) calls the main Nix projects
# `make install` before calling the Python bindings' `meson compile`, therefore
# ensuring that the needed Nix dynamic libraries are up-to-date

builddir=build

.PHONY: build
build: nix-install setup-done
	meson compile -C $(builddir)

.PHONY: test
test: nix-install setup-done
	meson test -C $(builddir) -v

.PHONY: clean
clean:
	rm -rf $(builddir)

# We include the main Nix projects Makefile.config file to know the $(libdir)
# variable, which is where Nix is installed in, which we can then use to setup
# the meson build
include ../Makefile.config

# We need the file to exist though
../Makefile.config:
	@# Throw a good error message in case ./configure hasn't been run yet
	@[[ -e ../config.status ]] || ( echo "The main Nix project needs to be configured first, see https://nixos.org/manual/nix/stable/contributing/hacking.html" && exit 1 )
	@# If ./configure is done, we can create the file ourselves
	$(MAKE) -C .. Makefile.config

.PHONY: setup
setup: nix-install
	@# Make meson be able to find the locally-installed Nix
	PKG_CONFIG_PATH=$(libdir)/pkgconfig:$$PKG_CONFIG_PATH meson setup $(builddir)

.PHONY: setup-done
setup-done:
	@# A better error message in case the build directory doesn't exist yet
	@[[ -e $(builddir) ]] || ( echo "Run 'make setup' once to configure the project build directory" && exit 1 )

.PHONY: nix-install
nix-install:
	@# The python bindings don't technically need an _entire_ Nix installation,
	@# but it seems non-trivial to pick out only exactly the files it actually needs
	$(MAKE) -C .. install
