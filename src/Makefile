all: nix nix-instantiate

SYSTEM = $(shell ./config.guess)

nix: nix.cc
	g++ -g -Wall -o nix nix.cc -ldb_cxx-4 -DSYSTEM=\"$(SYSTEM)\"

nix-instantiate: nix-instantiate.in
	sed "s/@SYSTEM@/$(SYSTEM)/" < $^ > $@
	chmod +x $@

clean:
	rm -f *.o nix nix-instantiate
