#!/usr/bin/env python3

import argparse
import json
import re
import sys


empty_anchor_regex = re.compile(r"\[\]\{#(?P<anchor>[^\}]+?)\}")
anchor_regex = re.compile(r"\[(?P<text>[^\]]+?)\]\{#(?P<anchor>[^\}]+?)\}")


def transform_anchors_html(content):
    content = empty_anchor_regex.sub(r'<a name="\g<anchor>"></a>', content)
    content = anchor_regex.sub(r'<a href="#\g<anchor>" id="\g<anchor>">\g<text></a>', content)
    return content


def transform_anchors_strip(content):
    content = empty_anchor_regex.sub(r'', content)
    content = anchor_regex.sub(r'\g<text>', content)
    return content


def map_contents_recursively(transformer, chapter):
    chapter["Chapter"]["content"] = transformer(chapter["Chapter"]["content"])
    for sub_item in chapter["Chapter"]["sub_items"]:
        map_contents_recursively(transformer, sub_item)


def supports_command(args):
    sys.exit(0)


def process_command(args):
    context, book = json.load(sys.stdin)
    transformer = transform_anchors_html if context["renderer"] == "html" else transform_anchors_strip
    for section in book["sections"]:
        map_contents_recursively(transformer, section)
    print(json.dumps(book))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="mdBook preprocessor adding anchors."
    )
    parser.set_defaults(command=process_command)

    subparsers = parser.add_subparsers()

    supports_parser = subparsers.add_parser("supports", help="Check if given renderer is supported")
    supports_parser.add_argument("renderer", type=str)
    supports_parser.set_defaults(command=supports_command)

    args = parser.parse_args()
    args.command(args)
