clean_files :=

clean:
	rm -fv $(clean_files)

dryclean:
	@echo $(clean_files)
