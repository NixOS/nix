%: %.in Makefile
	sed \
	 -e "s^@extra1\@^$(extra1)^g" \
	 -e "s^@prefix\@^$(prefix)^g" \
	 -e "s^@bindir\@^$(bindir)^g" \
	 -e "s^@sysconfdir\@^$(sysconfdir)^g" \
	 -e "s^@localstatedir\@^$(localstatedir)^g" \
	 -e "s^@datadir\@^$(datadir)^g" \
	 -e "s^@libexecdir\@^$(libexecdir)^g" \
	 -e "s^@storedir\@^$(storedir)^g" \
	 -e "s^@system\@^$(system)^g" \
	 -e "s^@shell\@^$(shell)^g" \
	 -e "s^@curl\@^$(curl)^g" \
	 -e "s^@bzip2\@^$(bzip2)^g" \
	 -e "s^@bunzip2\@^$(bunzip2)^g" \
	 -e "s^@perl\@^$(perl)^g" \
	 -e "s^@coreutils\@^$(coreutils)^g" \
	 -e "s^@tar\@^$(tar)^g" \
	 -e "s^@version\@^$(VERSION)^g" \
	 < $< > $@ || rm $@
	if test -x $<; then chmod +x $@; fi
