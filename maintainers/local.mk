
.PHONY: format
print-top-help += echo '  format: Format source code'

# This uses the cached .pre-commit-hooks.yaml file
format:
	@if ! type -p pre-commit &>/dev/null; then \
	  echo "make format: pre-commit not found. Please use \`nix develop\`."; \
	  exit 1; \
	fi; \
	if test -z "$$_NIX_PRE_COMMIT_HOOKS_CONFIG"; then \
	  echo "make format: _NIX_PRE_COMMIT_HOOKS_CONFIG not set. Please use \`nix develop\`."; \
	  exit 1; \
	fi; \
	pre-commit run --config $$_NIX_PRE_COMMIT_HOOKS_CONFIG --all-files
