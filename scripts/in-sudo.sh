#!/usr/bin/env bash

export_equal_string() {
    local equal_string=$1

    local variable_name value
    IFS='=' read -r -d $'\0' variable_name value <<< "$equal_string"
    value=${value:0:-1}
    eval "export $variable_name=$(printf '%q' "$value")"
}

while [ $# -gt 0 ]; do
    case "$1" in
        *=*) export_equal_string "$1"; shift ;;
        *) break ;;
    esac
done

"$@"
