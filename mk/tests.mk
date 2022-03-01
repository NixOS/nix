# Run program $1 as part of ‘make installcheck’.

test-deps =

define run-install-test

  installcheck: $1.test

  .PHONY: $1.test
  $1.test: $1 $(test-deps)
	@env TEST_NAME=$(basename $1) TESTS_ENVIRONMENT="$(tests-environment)" mk/run_test.sh $1 < /dev/null

endef

.PHONY: check installcheck

print-top-help += \
  echo "  check: Run unit tests"; \
  echo "  installcheck: Run functional tests";
