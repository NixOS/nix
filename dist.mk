ifdef PACKAGE_NAME

dist-name = $(PACKAGE_NAME)-$(PACKAGE_VERSION)

dist-files :=

dist: $(dist-name).tar.bz2

$(dist-name).tar.bz2: $(dist-files)
	$(suppress) tar cvfj $@ $(dist-files) --transform 's,^,$(dist-name)/,'

clean-files += $(dist-name).tar.bz2

print-top-help += echo "  dist: Generate a source distribution";

endif
