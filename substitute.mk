%: %.in Makefile
	sed \
	 -e "s^@prefix\@^$(prefix)^g" \
	 -e "s^@bindir\@^$(bindir)^g" \
	 -e "s^@sysconfdir\@^$(sysconfdir)^g" \
	 -e "s^@localstatedir\@^$(localstatedir)^g" \
	 -e "s^@datadir\@^$(datadir)^g" \
	 -e "s^@libexecdir\@^$(libexecdir)^g" \
	 -e "s^@storedir\@^$(storedir)^g" \
	 -e "s^@system\@^$(system)^g" \
	 -e "s^@wget\@^$(wget)^g" \
	 -e "s^@version\@^$(VERSION)^g" \
	 < $< > $@ || rm $@
	if test -x $<; then chmod +x $@; fi
