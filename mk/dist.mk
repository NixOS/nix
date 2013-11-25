dist_name = $(PACKAGE_NAME)-$(PACKAGE_VERSION)

dist_files :=

dist: $(dist_name).tar.bz2

$(dist_name).tar.bz2: $(dist_files)
	$(QUIET) tar cvfj $@ $(dist_files) --transform 's,^,$(dist_name)/,'

clean_files += $(dist_name).tar.bz2
