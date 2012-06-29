%: %.in Makefile
	sed \
	 -e "s^@abs_top_srcdir\@^$(abs_top_srcdir)^g" \
	 -e "s^@abs_top_builddir\@^$(abs_top_builddir)^g" \
	 -e "s^@extra1\@^$(extra1)^g" \
	 -e "s^@prefix\@^$(prefix)^g" \
	 -e "s^@bindir\@^$(bindir)^g" \
	 -e "s^@datadir\@^$(datadir)^g" \
	 -e "s^@sysconfdir\@^$(sysconfdir)^g" \
	 -e "s^@localstatedir\@^$(localstatedir)^g" \
	 -e "s^@datadir\@^$(datadir)^g" \
	 -e "s^@libdir\@^$(libdir)^g" \
	 -e "s^@libexecdir\@^$(libexecdir)^g" \
	 -e "s^@storedir\@^$(storedir)^g" \
	 -e "s^@system\@^$(system)^g" \
	 -e "s^@shell\@^$(bash)^g" \
	 -e "s^@curl\@^$(curl)^g" \
	 -e "s^@bzip2\@^$(bzip2)^g" \
	 -e "s^@xz\@^$(xz)^g" \
	 -e "s^@perl\@^$(perl)^g" \
	 -e "s^@perlFlags\@^$(perlFlags)^g" \
	 -e "s^@coreutils\@^$(coreutils)^g" \
	 -e "s^@sed\@^$(sed)^g" \
	 -e "s^@tar\@^$(tar)^g" \
	 -e "s^@gzip\@^$(gzip)^g" \
	 -e "s^@pv\@^$(pv)^g" \
	 -e "s^@tr\@^$(tr)^g" \
	 -e "s^@dot\@^$(dot)^g" \
	 -e "s^@xmllint\@^$(xmllint)^g" \
	 -e "s^@xmlflags\@^$(xmlflags)^g" \
	 -e "s^@xsltproc\@^$(xsltproc)^g" \
	 -e "s^@sqlite_bin\@^$(sqlite_bin)^g" \
	 -e "s^@version\@^$(VERSION)^g" \
	 -e "s^@perlbindings\@^$(perlbindings)^g" \
	 -e "s^@testPath\@^$(coreutils):$$(dirname $$(type -p expr))^g" \
	 < $< > $@ || rm $@
	if test -x $<; then chmod +x $@; fi
