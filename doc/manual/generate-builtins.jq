. | to_entries | sort_by(.key) | map(
  "  - `builtins." + .key + "` "
  + (.value.args | map("*" + . + "*") | join(" "))
  + "  \n\n"
  + (.value.doc | split("\n") | map("    " + . + "\n") | join("")) + "\n\n"
) | join("")
