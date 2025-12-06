#!/usr/bin/env python3
"""
Standalone markdown preprocessor for manpage generation.

Expands {{#include}} directives and handles @docroot@ references
without requiring mdbook.
"""

from pathlib import Path
import sys
import argparse
import re


def expand_includes(
    content: str,
    current_file: Path,
    source_root: Path,
    generated_root: Path | None,
    visited: set[Path] | None = None,
) -> str:
    """
    Recursively expand {{#include path}} directives.

    Args:
        content: Markdown content to process
        current_file: Path to the current file (for resolving relative includes)
        source_root: Root of the source directory
        generated_root: Root of generated files (for @generated@/ includes)
        visited: Set of already-visited files (for cycle detection)
    """
    if visited is None:
        visited = set()

    # Track current file to detect cycles
    visited.add(current_file.resolve())

    lines = []
    include_pattern = re.compile(r'^\s*\{\{#include\s+(.+?)\}\}\s*$')

    for line in content.splitlines(keepends=True):
        match = include_pattern.match(line)
        if not match:
            lines.append(line)
            continue

        # Found an include directive
        include_path_str = match.group(1).strip()

        # Resolve the include path
        if include_path_str.startswith("@generated@/"):
            # Generated file
            if generated_root is None:
                raise ValueError(
                    f"Cannot resolve @generated@ path '{include_path_str}' "
                    f"without --generated-root"
                )
            include_path = generated_root / include_path_str[12:]
        else:
            # Relative to current file
            include_path = (current_file.parent / include_path_str).resolve()

        # Check for cycles
        if include_path.resolve() in visited:
            raise RuntimeError(
                f"Include cycle detected: {include_path} is already being processed"
            )

        # Check that file exists
        if not include_path.exists():
            raise FileNotFoundError(
                f"Include file not found: {include_path_str}\n"
                f"  Resolved to: {include_path}\n"
                f"  From: {current_file}"
            )

        # Recursively expand the included file
        included_content = include_path.read_text()
        expanded = expand_includes(
            included_content,
            include_path,
            source_root,
            generated_root,
            visited.copy(),  # Copy visited set for this branch
        )
        lines.append(expanded)
        # Add newline if the included content doesn't end with one
        if not expanded.endswith('\n'):
            lines.append('\n')

    return ''.join(lines)


def resolve_docroot(content: str, current_file: Path, source_root: Path) -> str:
    """
    Replace @docroot@ with nix.dev URL and convert .md to .html.

    For manpages, absolute URLs are more useful than relative paths since
    manpages are viewed standalone. lowdown will display these as proper
    references in the manpage output.
    """
    # Use the latest nix.dev documentation URL
    # This matches what users would actually want to reference from a manpage
    docroot_url = "https://nix.dev/manual/nix/latest"

    # Replace @docroot@ with the base URL
    content = content.replace("@docroot@", docroot_url)

    # Convert .md extensions to .html for web links
    # Use lookahead to ensure that .md occurs before a fragment or a possible URL end.
    content = re.sub(
        r'(https://nix\.dev/[^)\s]*?)\.md(?=[#)\s]|$)',
        r'\1.html',
        content
    )

    return content


def resolve_at_escapes(content: str) -> str:
    """Replace @_at_ with @"""
    return content.replace("@_at_", "@")


def process_file(
    input_file: Path,
    source_root: Path,
    generated_root: Path | None,
) -> str:
    """Process a single markdown file."""
    content = input_file.read_text()

    # Expand includes
    content = expand_includes(content, input_file, source_root, generated_root)

    # Resolve @docroot@ references
    content = resolve_docroot(content, input_file, source_root)

    # Resolve @_at_ escapes
    content = resolve_at_escapes(content)

    return content


def main():
    parser = argparse.ArgumentParser(
        description="Expand markdown includes for manpage generation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Expand a manpage source file
  %(prog)s \\
    --source-root doc/manual/source \\
    --generated-root build/doc/manual/source \\
    doc/manual/source/command-ref/nix-store/query.md

  # Pipe to lowdown for manpage generation
  %(prog)s -s doc/manual/source -g build/doc/manual/source \\
    doc/manual/source/command-ref/nix-env.md | \\
    lowdown -sT man -M section=1 -o nix-env.1
        """,
    )
    parser.add_argument(
        "input_file",
        type=Path,
        help="Input markdown file to process",
    )
    parser.add_argument(
        "-s", "--source-root",
        type=Path,
        required=True,
        help="Root directory of markdown sources",
    )
    parser.add_argument(
        "-g", "--generated-root",
        type=Path,
        help="Root directory of generated files (for @generated@/ includes)",
    )
    parser.add_argument(
        "-o", "--output",
        type=Path,
        help="Output file (default: stdout)",
    )

    args = parser.parse_args()

    # Validate paths
    if not args.input_file.exists():
        print(f"Error: Input file not found: {args.input_file}", file=sys.stderr)
        return 1

    if not args.source_root.is_dir():
        print(f"Error: Source root is not a directory: {args.source_root}", file=sys.stderr)
        return 1

    if args.generated_root and not args.generated_root.is_dir():
        print(f"Error: Generated root is not a directory: {args.generated_root}", file=sys.stderr)
        return 1

    try:
        # Process the file
        output = process_file(args.input_file, args.source_root, args.generated_root)

        # Write output
        if args.output:
            args.output.write_text(output)
        else:
            print(output, end='')

        return 0

    except Exception as e:
        print(f"Error processing {args.input_file}: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc(file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
