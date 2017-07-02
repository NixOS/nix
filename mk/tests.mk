# Run program $1 as part of ‘make installcheck’.

logs:
	@mkdir -p logs

clean-files += logs

define run-install-test
.PHONY: $(1)
.ONESHELL: $(1)
$(1): logs
	@echo "Running test: $$(@)"
	@$$(tests-environment) $$(@) > logs/$$(subst /,_,$$(@)).log 2>&1

installcheck: $(1)

endef

.PHONY: check installcheck
