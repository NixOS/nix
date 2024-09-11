
.PHONY: format
print-top-help += echo '  format: Format source code'

# This uses the cached .pre-commit-hooks.yaml file
fmt_script := $(d)/format.sh
format:
	@$(fmt_script)
