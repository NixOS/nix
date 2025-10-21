# For some reason, backticks in the JSON schema are being escaped rather
# than being kept as intentional code spans. This removes all backtick
# escaping, which is an ugly solution, but one that is fine, because we
# are not using backticks for any other purpose.
s/\\`/`/g

# The way that semi-external references are rendered (i.e. ones to
# sibling schema files, as opposed to separate website ones, is not nice
# for humans. Replace it with a nice relative link within the manual
# instead.
#
# As we have more such relative links, more replacements of this nature
# should appear below.
s^#/\$defs/\(regular\|symlink\|directory\)^In this schema^g
s^\(./hash-v1.yaml\)\?#/$defs/algorithm^[JSON format for `Hash`](./hash.html#algorithm)^g
