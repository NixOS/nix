git-hashing-tests := \
  $(d)/simple.sh \
  $(d)/fetching.sh

install-tests-groups += git-hashing

clean-files += \
  $(d)/config.nix
