# Utility function for recursively finding files, e.g.
# ‘$(call rwildcard, path/to/dir, *.c *.h)’.
rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))

# Given a file name, produce the corresponding dependency file
# (e.g. ‘foo/bar.o’ becomes ‘foo/.bar.o.dep’).
filename-to-dep = $(dir $1).$(notdir $1).dep

# Return the full path to a program by looking it up in $PATH, or the
# empty string if not found.
find-program = $(shell for i in $$(IFS=: ; echo $$PATH); do p=$$i/$(strip $1); if [ -e $$p ]; then echo $$p; break; fi; done)

# Ensure that the given string ends in a single slash.
add-trailing-slash = $(patsubst %/,%,$(1))/
