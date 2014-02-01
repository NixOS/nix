clean_files :=

clean:
	$(suppress) rm -fv -- $(clean_files)

dryclean:
	@for i in $(clean_files); do if [ -e $$i ]; then echo $$i; fi; done | sort

print-top-help += \
  echo "  clean: Delete generated files"; \
  echo "  dryclean: Show what files would be deleted by 'make clean'";
