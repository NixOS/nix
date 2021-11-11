$(foreach i, $(wildcard src/nlohmann/*.hpp), \
  $(eval $(call install-file-in, $(i), $(includedir)/nlohmann, 0644)))
