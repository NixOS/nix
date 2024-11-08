%.gen.hh: %
	@echo 'R"__NIX_STR(' >> $@.tmp
	$(trace-gen) cat $< >> $@.tmp
	@echo ')__NIX_STR"' >> $@.tmp
	@mv $@.tmp $@
