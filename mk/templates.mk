# Create the file $(1) from $(1).in by running config.status (which
# substitutes all ‘@var@’ variables set by the configure script).
define instantiate-template =

  $(1): $(1).in
	./config.status --file $(1)

endef
