all: nix nix-instantiate

SYSTEM = $(shell ./config.guess)

nix: nix.o md5.o
	g++ -g -o $@ $^ -ldb_cxx-4

%.o: %.cc
	g++ -g -Wall -o $@ -c $< -DSYSTEM=\"$(SYSTEM)\"

%.o: %.c
	gcc -g -Wall -o $@ -c $< -DSYSTEM=\"$(SYSTEM)\"

md5.o: md5.c md5.h

nix-instantiate: nix-instantiate.in
	sed "s/@SYSTEM@/$(SYSTEM)/" < $^ > $@
	chmod +x $@

clean:
	rm -f *.o nix nix-instantiate
