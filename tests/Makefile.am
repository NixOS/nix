TEST_ROOT = $(shell pwd)/test-tmp

TESTS_ENVIRONMENT = TEST_ROOT=$(TEST_ROOT) \
  NIX_STORE_DIR=$(TEST_ROOT)/store \
  NIX_DATA_DIR=$(TEST_ROOT)/data \
  NIX_LOG_DIR=$(TEST_ROOT)/log \
  NIX_STATE_DIR=$(TEST_ROOT)/state \
  NIX_DB_DIR=$(TEST_ROOT)/db \
  TOP=$(shell pwd)/.. \
  $(SHELL) -e -x

simple.sh: simple.nix

TESTS = init.sh simple.sh

include ../substitute.mk

EXTRA_DIST = \
  simple.nix.in simple.builder.sh
