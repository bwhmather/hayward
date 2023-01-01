import itertools

import clang.cindex
from hayward_lint import (
    INCLUDE_ROOT,
    assert_equal,
    enumerate_header_paths,
    read_ast_from_path,
)


def test():
    header_paths = [
        header_path
        for header_path in enumerate_header_paths()
        if header_path.is_relative_to(INCLUDE_ROOT / "tree")
    ]
    assert header_paths

    decls = {}

    for header_path in header_paths:
        header = read_ast_from_path(header_path)
        prefix = header_path.stem

        header_decls = [
            node.spelling
            for node in header.cursor.get_children()
            if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
            and node.location.file.name == header.spelling
        ]

        header_decls = [
            name[len(prefix) + 1 :]
            for name in header_decls
            if name.startswith(f"{prefix}_")
        ]

        decls[header_path] = header_decls

    for header_a_path, header_b_path in itertools.combinations(header_paths, 2):
        header_a_decls = decls[header_a_path]
        header_b_decls = decls[header_b_path]

        header_a_decls = [name for name in header_a_decls if name in header_b_decls]
        header_b_decls = [name for name in header_b_decls if name in header_a_decls]

        assert_equal(
            header_a_decls,
            header_b_decls,
            msg=f"Order of declarations in {header_a_path.name} does not match {header_b_path.name}",
        )


if __name__ == "__main__":
    test()
