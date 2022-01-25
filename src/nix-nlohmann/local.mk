$(foreach i, $(wildcard src/nix-nlohmann/*.hpp), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix-nlohmann, 0644)))
