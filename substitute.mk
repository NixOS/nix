%: %.in Makefile
	sed \
	 -e "s^@prefix\@^$(prefix)^g" \
	 -e "s^@bindir\@^$(bindir)^g" \
	 -e "s^@sysconfdir\@^$(sysconfdir)^g" \
	 -e "s^@localstatedir\@^$(localstatedir)^g" \
	 -e "s^@datadir\@^$(datadir)^g" \
	 -e "s^@libexecdir\@^$(libexecdir)^g" \
	 -e "s^@system\@^$(system)^g" \
	 -e "s^@wget\@^$(wget)^g" \
	 < $< > $@ || rm $@
	chmod +x $@
