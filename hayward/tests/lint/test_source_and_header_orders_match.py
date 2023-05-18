import difflib
import itertools
import sys
import traceback

import clang.cindex
from hayward_lint import (
    PROJECT_ROOT,
    assert_equal,
    enumerate_header_paths,
    enumerate_source_paths,
    header_path_for_source_path,
    read_ast_from_path,
    resolve_clang_path,
)


def test():
    passed = True

    for source_path in itertools.chain(enumerate_source_paths()):
        if source_path.match("hayward/src/commands.c"):
            # TODO Command modules share a single header.
            continue

        if source_path.is_relative_to("hayward/src/commands"):
            # TODO Command modules share a single header.
            continue

        header_path = header_path_for_source_path(source_path)
        if header_path is None:
            continue

        header = read_ast_from_path(header_path)
        source = read_ast_from_path(source_path)

        header_decls = [
            node.spelling
            for node in header.cursor.get_children()
            if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
            and resolve_clang_path(node.location.file.name)
            == resolve_clang_path(header.spelling)
        ]
        source_defs = [
            node.spelling
            for node in source.cursor.get_children()
            if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
            and node.is_definition()
            and node.storage_class != clang.cindex.StorageClass.STATIC
            and resolve_clang_path(node.location.file.name)
            == resolve_clang_path(source.spelling)
        ]

        header_decls = [symbol for symbol in header_decls if symbol in source_defs]
        source_defs = [symbol for symbol in source_defs if symbol in header_decls]

        if header_decls != source_defs:
            msg = "======================================================================\n"
            msg += f"FAIL: test_source_and_header_orders_match: {source_path.relative_to(PROJECT_ROOT)}\n"
            msg += "----------------------------------------------------------------------\n"
            msg += f"Order of definitions in {source_path.relative_to(PROJECT_ROOT)} does not match order of declarations in {header_path.relative_to(PROJECT_ROOT)}:\n"

            for diff_line in difflib.ndiff(header_decls, source_defs):
                if diff_line.startswith("?"):
                    continue
                msg += f"  {diff_line}\n"
            msg += "\n"

            print(msg, file=sys.stderr)
            passed = False

    return passed


if __name__ == "__main__":
    sys.exit(0 if test() else 1)
