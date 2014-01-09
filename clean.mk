clean_files :=

clean:
	$(QUIET) rm -fv -- $(clean_files)

dryclean:
	@for i in $(clean_files); do if [ -e $$i ]; then echo $$i; fi; done | sort
