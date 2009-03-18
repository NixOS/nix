source common.sh

clearStore

# Produce an escaped log file.
set -x
$nixbuild --log-type escapes -vv dependencies.nix 2> $TEST_ROOT/log.esc

# Convert it to an XML representation.
$TOP/src/nix-log2xml/nix-log2xml < $TEST_ROOT/log.esc > $TEST_ROOT/log.xml

# Is this well-formed XML?
if test "$xmllint" != "false"; then
    $xmllint $xmlflags --noout $TEST_ROOT/log.xml || fail "malformed XML"
fi

# Convert to HTML.
if test "$xsltproc" != "false"; then
    (cd $TOP/src/nix-log2xml && $xsltproc mark-errors.xsl - | $xsltproc log2html.xsl -) < $TEST_ROOT/log.xml > $TEST_ROOT/log.html
    # Ideally we would check that the generated HTML is valid...

    # A few checks...
    grep "<code>.*FOO" $TEST_ROOT/log.html || fail "bad HTML output"
fi
