# Run program $1 as part of ‘make installcheck’.
define run-install-test

  installcheck: $1.test

  .PHONY: $1.test
  $1.test: $1 tests/common.sh tests/init.sh
	@env TEST_NAME=$1 TESTS_ENVIRONMENT="$(tests-environment)" mk/run_test.sh $1

endef

.PHONY: check installcheck
