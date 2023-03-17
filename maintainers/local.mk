
.PHONY: format
print-top-help += echo '  format: Format source code'

format:
	@if ! type -p pre-commit &>/dev/null; then \
	  echo "make format: pre-commit not found. Please use \`nix develop\`."; \
	  exit 1; \
	fi; \
	pre-commit run --all-files --hook-stage manual
