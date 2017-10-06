# Run program $1 as part of ‘make installcheck’.
define run-install-test

  installcheck: $1

  _installcheck-list += $1

endef

# Color code from https://unix.stackexchange.com/a/10065
installcheck:
	@total=0; failed=0; \
	red=""; \
	green=""; \
	normal=""; \
	if [ -t 1 ]; then \
		ncolors="$$(tput colors)"; \
		if [ -n "$$ncolors" ] && [ "$$ncolors" -ge 8 ]; then \
			red="$$(tput setaf 1)"; \
			green="$$(tput setaf 2)"; \
			normal="$$(tput sgr0)"; \
		fi; \
	fi; \
	for i in $(_installcheck-list); do \
	  total=$$((total + 1)); \
	  printf "running test $$i..."; \
	  log="$$(cd $$(dirname $$i) && $(tests-environment) $$(basename $$i) 2>&1)"; \
	  if [ $$? -eq 0 ]; then \
	    echo "[$${green}PASS$$normal]"; \
	  else \
	    echo "[$${red}FAIL$$normal]"; \
	    echo "$$log" | sed 's/^/    /'; \
	    failed=$$((failed + 1)); \
	  fi; \
	done; \
	if [ "$$failed" != 0 ]; then \
	  echo "$${red}$$failed out of $$total tests failed $$normal"; \
	  exit 1; \
	else \
		echo "$${green}All tests succeeded"; \
	fi

.PHONY: check installcheck
