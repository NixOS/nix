%: %.in Makefile
	sed \
	 -e s^@prefix\@^$(prefix)^g \
	 -e s^@bindir\@^$(bindir)^g \
	 -e s^@sysconfdir\@^$(sysconfdir)^g \
	 -e s^@localstatedir\@^$(localstatedir)^g \
	 < $< > $@ || rm $@
	chmod +x $@
