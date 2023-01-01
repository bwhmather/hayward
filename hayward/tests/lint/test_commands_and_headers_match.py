import clang.cindex
from hayward_lint import (
    INCLUDE_ROOT,
    SOURCE_ROOT,
    assert_equal,
    enumerate_source_paths,
    read_ast_from_path,
    resolve_clang_path,
)


def test():
    source_paths = [SOURCE_ROOT / "commands.c"]
    source_paths += [
        source_path
        for source_path in enumerate_source_paths()
        if source_path.is_relative_to(SOURCE_ROOT / "commands")
    ]
    assert len(source_paths) > 1

    source_defs = []
    for source_path in source_paths:
        source = read_ast_from_path(source_path)
        source_defs += [
            node.spelling
            for node in source.cursor.get_children()
            if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
            and node.is_definition()
            and node.storage_class != clang.cindex.StorageClass.STATIC
            and resolve_clang_path(node.location.file.name)
            == resolve_clang_path(source.spelling)
        ]

    header_path = INCLUDE_ROOT / "commands.h"
    header = read_ast_from_path(header_path)
    header_decls = [
        node.spelling
        for node in header.cursor.get_children()
        if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
        and resolve_clang_path(node.location.file.name)
        == resolve_clang_path(header.spelling)
    ]

    assert_equal(set(header_decls), set(source_defs))


if __name__ == "__main__":
    test()
