#!/usr/bin/env python3

from pathlib import Path
import json
import os, os.path
import sys
import typing as t

name = 'substitute.py'

def log(*args: t.Any, **kwargs: t.Any) -> None:
    kwargs['file'] = sys.stderr
    print(f'{name}:', *args, **kwargs)

def do_include(content: str, relative_md_path: Path, source_root: Path, search_path: Path) -> str:
    assert not relative_md_path.is_absolute(), f'{relative_md_path=} from mdbook should be relative'

    md_path_abs = source_root / relative_md_path
    var_abs = md_path_abs.parent
    assert var_abs.is_dir(), f'supposed directory {var_abs} is not a directory (cwd={os.getcwd()})'

    lines = []
    for l in content.splitlines(keepends=True):
        if l.strip().startswith("{{#include "):
            requested = l.strip()[11:][:-2]
            if requested.startswith("@generated@/"):
                included = search_path / Path(requested[12:])
                requested = included.relative_to(search_path)
            else:
                included = source_root / relative_md_path.parent / requested
                requested = included.resolve().relative_to(source_root)
            assert included.exists(), f"{requested} not found at {included}"
            lines.append(do_include(included.read_text(), requested, source_root, search_path) + "\n")
        else:
            lines.append(l)
    return "".join(lines)

def recursive_replace(data: dict[str, t.Any], book_root: Path, search_path: Path) -> dict[str, t.Any]:
    match data:
        case {'sections': sections}:
            return data | dict(
                sections = [recursive_replace(section, book_root, search_path) for section in sections],
            )
        case {'Chapter': chapter}:
            path_to_chapter = Path(chapter['path'])
            chapter_content = chapter['content']

            return data | dict(
                Chapter = chapter | dict(
                    # first process includes. this must happen before docroot processing since
                    # mdbook does not see these included files, only the final agglomeration.
                    content = do_include(
                        chapter_content,
                        path_to_chapter,
                        book_root,
                        search_path
                    ).replace(
                        '@docroot@',
                        ("../" * len(path_to_chapter.parent.parts) or "./")[:-1]
                    ).replace(
                        '@_at_',
                        '@'
                    ),
                    sub_items = [
                        recursive_replace(sub_item, book_root, search_path)
                        for sub_item in chapter['sub_items']
                    ],
                ),
            )

        case rest:
            assert False, f'should have been called on a dict, not {type(rest)=}\n\t{rest=}'

def main() -> None:


    if len(sys.argv) > 1 and sys.argv[1] == 'supports':
        return 0

    # includes pointing into @generated@ will look here
    search_path = Path(os.environ['MDBOOK_SUBSTITUTE_SEARCH'])

    if len(sys.argv) > 1 and sys.argv[1] == 'summary':
        print(do_include(
                        sys.stdin.read(),
                        Path('source/SUMMARY.md'),
                        Path(sys.argv[2]).resolve(),
                        search_path))
        return

    # mdbook communicates with us over stdin and stdout.
    # It splorks us a JSON array, the first element describing the context,
    # the second element describing the book itself,
    # and then expects us to send it the modified book JSON over stdout.

    context, book = json.load(sys.stdin)

    # book_root is the directory where book contents leave (ie, source/)
    book_root = Path(context['root']) / context['config']['book']['src']

    # Find @var@ in all parts of our recursive book structure.
    replaced_content = recursive_replace(book, book_root, search_path)

    replaced_content_str = json.dumps(replaced_content)

    # Give mdbook our changes.
    print(replaced_content_str)

try:
    sys.exit(main())
except AssertionError as e:
    print(f'{name}: INTERNAL ERROR in mdbook preprocessor: {e}', file=sys.stderr)
    print(f'this is a bug in {name}', file=sys.stderr)
    raise
