# Run program $1 as part of ‘make installcheck’.

test-deps =

define run-bash

  .PHONY: $1
  $1: $2
	@env BASH=$(bash) $(bash) $3 < /dev/null

endef

define run-test

  $(eval $(call run-bash,$1.test,$1 $(test-deps),mk/run-test.sh $1 $2))
  $(eval $(call run-bash,$1.test-debug,$1 $(test-deps),mk/debug-test.sh $1 $2))

endef

define run-test-group

  .PHONY: $1.test-group

endef

.PHONY: check installcheck

print-top-help += \
  echo "  check: Run unit tests"; \
  echo "  installcheck: Run functional tests";
