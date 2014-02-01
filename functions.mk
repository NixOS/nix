# Utility function for recursively finding files, e.g.
# ‘$(call rwildcard, path/to/dir, *.c *.h)’.
rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))

# Given a file name, produce the corresponding dependency file
# (e.g. ‘foo/bar.o’ becomes ‘foo/.bar.o.dep’).
filename-to-dep = $(dir $1).$(notdir $1).dep
