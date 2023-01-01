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


def _check_path(source_path, /):
    if source_path.match("hayward/src/commands.c"):
        # TODO Command modules share a single header.
        return

    if source_path.is_relative_to("hayward/src/commands"):
        # TODO Command modules share a single header.
        return

    header_path = header_path_for_source_path(source_path)
    if header_path is None:
        print(f"WARNING: No header for {source_path}", file=sys.stderr)
        return

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

    assert_equal(set(header_decls), set(source_defs))
    assert_equal("\n".join(header_decls), "\n".join(source_defs))


def test():
    passed = True

    for source_path in itertools.chain(
        enumerate_header_paths(),
        enumerate_source_paths(),
    ):
        try:
            _check_path(source_path)
        except Exception:
            print(
                "======================================================================\n"
                + f"FAIL: {source_path.relative_to(PROJECT_ROOT)}\n"
                + "----------------------------------------------------------------------\n",
                file=sys.stderr,
            )
            traceback.print_exc()
            passed = False

    return passed


if __name__ == "__main__":
    sys.exit(0 if test() else 1)
