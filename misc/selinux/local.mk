ifneq ($(HAVE_POLICYTOOLS),)
  policies += nixpolicy

  nixpolicy_DIR := $(d)

  nixpolicy_NAME := nix
endif
