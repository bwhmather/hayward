import functools
import itertools
import json
import os
import pathlib
import re
import shlex
import unittest

import clang.cindex

SOURCE_ROOT = pathlib.Path(os.environ["MESON_SOURCE_ROOT"]).resolve()
BUILD_ROOT = pathlib.Path(os.environ["MESON_BUILD_ROOT"]).resolve()

HAYWARD_ROOT = SOURCE_ROOT / pathlib.Path("hayward/src")
HAYWARD_INCLUDE_ROOT = SOURCE_ROOT / pathlib.Path("hayward/include/hayward")


_raw_commands = json.loads((BUILD_ROOT / "compile_commands.json").read_text())
CPP_ARGS = {}
for _command in _raw_commands:
    _path = pathlib.Path(_command["directory"]) / _command["file"]
    _path = _path.resolve()
    CPP_ARGS[_path] = [
        arg
        for arg in shlex.split(_command["command"])
        if arg.startswith(("-I", "-D", "-std"))
    ]


INDEX = clang.cindex.Index.create()


def enumerate_source_files():
    files = [
        source_file.relative_to(SOURCE_ROOT)
        for source_file in HAYWARD_ROOT.glob("**/*.c")
    ]
    yield from sorted(files)


def enumerate_header_files():
    files = [
        source_file.relative_to(SOURCE_ROOT)
        for source_file in HAYWARD_INCLUDE_ROOT.glob("**/*.h")
    ]
    yield from sorted(files)


def header_path_for_source_path(source_path, /):
    path = (
        HAYWARD_INCLUDE_ROOT
        / (SOURCE_ROOT / source_path).relative_to(HAYWARD_ROOT).parent
        / (source_path.stem + ".h")
    ).relative_to(SOURCE_ROOT)

    if not (SOURCE_ROOT / path).exists():
        return None

    return path


def source_path_for_header_path(header_path, /):
    path = (
        HAYWARD_ROOT
        / (SOURCE_ROOT / header_path).relative_to(HAYWARD_INCLUDE_ROOT)
        / (header_path.stem + ".c")
    ).relative_to(SOURCE_ROOT)

    if not path.exists():
        return None

    return path


@functools.lru_cache(maxsize=None)
def parse_header_file(header_path):
    source_path = source_path_for_header_path(header_path)
    if source_path is None:
        source_path = pathlib.Path("hayward/src/server.c")

    args = CPP_ARGS[SOURCE_ROOT / source_path]
    return INDEX.parse(SOURCE_ROOT / header_path, args=args)


@functools.lru_cache(maxsize=None)
def parse_source_file(source_path):
    args = CPP_ARGS[SOURCE_ROOT / source_path]
    return INDEX.parse(SOURCE_ROOT / source_path, args=args)


def normalize_clang_path(path):
    path = pathlib.Path(path)
    if not path.is_absolute():
        path = BUILD_ROOT / path
    path = path.resolve()
    return path


class DeclarationOrderTestCase(unittest.TestCase):
    maxDiff = 2000

    def test_source_and_header_orders_match(self):
        for source_path in enumerate_source_files():
            with self.subTest(file=source_path):
                if source_path.match("hayward/src/commands.c"):
                    self.skipTest("Command modules share a single header")

                if source_path.is_relative_to("hayward/src/commands"):
                    self.skipTest("Command modules share a single header")

                header_path = header_path_for_source_path(source_path)
                if header_path is None:
                    self.skipTest(f"No header for {source_path}")

                header = parse_header_file(header_path)
                source = parse_source_file(source_path)

                header_decls = [
                    node.spelling
                    for node in header.cursor.get_children()
                    if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
                    and normalize_clang_path(node.location.file.name)
                    == normalize_clang_path(header.spelling)
                ]
                source_defs = [
                    node.spelling
                    for node in source.cursor.get_children()
                    if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
                    and node.is_definition()
                    and node.storage_class != clang.cindex.StorageClass.STATIC
                    and normalize_clang_path(node.location.file.name)
                    == normalize_clang_path(source.spelling)
                ]

                self.assertEqual(set(header_decls), set(source_defs))
                self.assertEqual("\n".join(header_decls), "\n".join(source_defs))

    def test_tree_header_orders_match(self):
        header_paths = [
            header_path
            for header_path in enumerate_header_files()
            if header_path.is_relative_to("hayward/include/hayward/tree")
        ]

        decls = {}

        for header_path in header_paths:
            header = parse_header_file(header_path)
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
            with self.subTest(header_a=header_a_path, header_b=header_b_path):
                header_a_decls = decls[header_a_path]
                header_b_decls = decls[header_b_path]

                header_a_decls = [
                    name for name in header_a_decls if name in header_b_decls
                ]
                header_b_decls = [
                    name for name in header_b_decls if name in header_a_decls
                ]

                self.assertEqual(header_a_decls, header_b_decls)

    def test_commands_and_headers_match(self):
        source_paths = [pathlib.Path("hayward/src/commands.c")]
        source_paths += [
            source_path
            for source_path in enumerate_source_files()
            if source_path.is_relative_to("hayward/src/commands")
        ]

        source_defs = []
        for source_path in source_paths:
            source = parse_source_file(source_path)
            source_defs += [
                node.spelling
                for node in source.cursor.get_children()
                if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
                and node.is_definition()
                and node.storage_class != clang.cindex.StorageClass.STATIC
                and normalize_clang_path(node.location.file.name)
                == normalize_clang_path(source.spelling)
            ]

        header_path = pathlib.Path("hayward/include/hayward/commands.h")
        header = parse_header_file(header_path)
        header_decls = [
            node.spelling
            for node in header.cursor.get_children()
            if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
            and normalize_clang_path(node.location.file.name)
            == normalize_clang_path(header.spelling)
        ]

        self.assertEqual(set(header_decls), set(source_defs))


class IncludeTestCase(unittest.TestCase):
    def test_no_circular_includes(self):
        pattern = re.compile('^#include "(hayward/.*\\.h)"$', flags=re.MULTILINE)

        # Build dependency graph.
        graph = {}
        for header in enumerate_header_files():
            name = str(header.relative_to("hayward/include"))
            path = SOURCE_ROOT / header
            source = path.read_text()
            graph[name] = {match.group(1) for match in pattern.finditer(source)}

        # Check for cycles.
        visited = set()
        stack = []
        for root in graph:
            queue = {root}
            visited = set()
            while queue:
                dep = queue.pop()
                visited.add(dep)
                deps = graph[dep]
                self.assertNotIn(root, deps)
                queue.update(deps.difference(visited))


if __name__ == "__main__":
    unittest.main(verbosity=2)
