import itertools
import sys

import clang.cindex
from hayward_lint import (
    PROJECT_ROOT,
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

        header_decls = {
            node.spelling
            for node in header.cursor.get_children()
            if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
            and resolve_clang_path(node.location.file.name)
            == resolve_clang_path(header.spelling)
        }
        source_defs = {
            node.spelling
            for node in source.cursor.get_children()
            if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
            and node.is_definition()
            and node.storage_class != clang.cindex.StorageClass.STATIC
            and resolve_clang_path(node.location.file.name)
            == resolve_clang_path(source.spelling)
        }

        if header_decls != source_defs:
            msg = "======================================================================\n"
            msg += f"FAIL: test_source_and_header_contents_match: {source_path.relative_to(PROJECT_ROOT)}\n"
            msg += "----------------------------------------------------------------------\n"

            msg += "Source file does not match header.\n\n"

            if header_decls - source_defs:
                msg += f"The following symbols were declared in {header_path.relative_to(PROJECT_ROOT)} but not defined in {source_path.relative_to(PROJECT_ROOT)}:\n"
                for symbol in header_decls - source_defs:
                    msg += f"  - {symbol}\n"
                msg += "\n"

            if source_defs - header_decls:
                msg += f"The following symbols were defined in {source_path.relative_to(PROJECT_ROOT)} but not declared in {header_path.relative_to(PROJECT_ROOT)}:\n"
                for symbol in source_defs - header_decls:
                    msg += f"  - {symbol}\n"
                msg += "\n"

            print(msg, file=sys.stderr)
            passed = False

    return passed


if __name__ == "__main__":
    sys.exit(0 if test() else 1)
