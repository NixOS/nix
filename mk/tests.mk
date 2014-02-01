# Run program $1 as part of ‘make installcheck’.
define run-install-test =

  installcheck: $1

  # Run the test in its own directory to mimick Automake behaviour.
  $1.run: $1 $(_PREV_TEST)

  _installcheck-list += $1

endef

installcheck:
	@total=0; failed=0; for i in $(_installcheck-list); do \
	  total=$$((total + 1)); \
	  echo "running test $$i"; \
	  if (cd $$(dirname $$i) && $(tests-environment) $$(basename $$i)); then \
	    echo "PASS: $$i"; \
	  else \
	    echo "FAIL: $$i"; \
	    failed=$$((failed + 1)); \
	  fi; \
	done; \
	if [ "$$failed" != 0 ]; then \
	  echo "$$failed out of $$total tests failed "; \
	  exit 1; \
	fi

.PHONY: check installcheck
