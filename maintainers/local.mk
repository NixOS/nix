
.PHONY: format
print-top-help += echo '  format: Format source code'

# This uses the cached .pre-commit-hooks.yaml file
format-quick:
	@if ! type -p pre-commit &>/dev/null; then \
	  echo "make format: pre-commit not found. Please use \`nix develop\`."; \
	  exit 1; \
	fi; \
	pre-commit run --all-files

format:
	nix develop --command -- pre-commit run --all-files
