#!/usr/bin/env python3
"""Generate redirects.js from template and JSON data."""

import sys

template_path, json_path, output_path = sys.argv[1:]

with open(json_path) as f:
    json_content = f.read().rstrip()

with open(template_path) as f:
    template = f.read()

with open(output_path, 'w') as f:
    f.write(template.replace('@REDIRECTS_JSON@', json_content))
