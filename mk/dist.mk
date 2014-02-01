ifdef PACKAGE_NAME

dist-name = $(PACKAGE_NAME)-$(PACKAGE_VERSION)

dist: $(dist-name).tar.bz2 $(dist-name).tar.xz

$(dist-name).tar.bz2: $(dist-files)
	$(trace-gen) tar cfj $@ $(sort $(dist-files)) --transform 's,^,$(dist-name)/,'

$(dist-name).tar.xz: $(dist-files)
	$(trace-gen) tar cfJ $@ $(sort $(dist-files)) --transform 's,^,$(dist-name)/,'

clean-files += $(dist-name).tar.bz2 $(dist-name).tar.xz

print-top-help += echo "  dist: Generate a source distribution";

endif
