# This file is only active for `./configure --disable-tests`.
# Running `make check` or `make installcheck` would indicate a mistake in the
# caller.

installcheck:
	@echo "Tests are disabled. Configure without '--disable-tests', or avoid calling 'make installcheck'."
	@exit 1

# This currently has little effect.
check:
	@echo "Tests are disabled. Configure without '--disable-tests', or avoid calling 'make check'."
	@exit 1
