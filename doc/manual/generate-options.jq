. | to_entries | sort_by(.key) | map(
  "  - `" + .key + "`  \n"
  + (.value.description | split("\n") | map("    " + . + "\n") | join("")) + "\n\n"
  + "    **Default**: " + (
      if .value.value == "" or .value.value == []
      then "*empty*"
      elif (.value.value | type) == "array"
      then "`" + (.value.value | join(" ")) + "`"
      else "`" + (.value.value | tostring) + "`" end)
  + "\n\n"
) | join("")
