import functools
import json
import os
import pathlib
import shlex
import unittest

import clang.cindex

SOURCE_ROOT = pathlib.Path(os.environ["MESON_SOURCE_ROOT"]).resolve()
BUILD_ROOT = pathlib.Path(os.environ["MESON_BUILD_ROOT"]).resolve()

HAYWARD_ROOT = SOURCE_ROOT / pathlib.Path("hayward")
HAYWARD_INCLUDE_ROOT = SOURCE_ROOT / pathlib.Path("include/hayward")


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


def resolve_header_path(source_path, /):
    path = (
        HAYWARD_INCLUDE_ROOT
        / (SOURCE_ROOT / source_path).relative_to(HAYWARD_ROOT).parent
        / (source_path.stem + ".h")
    ).relative_to(SOURCE_ROOT)

    if not (SOURCE_ROOT / path).exists():
        return None

    return path


def resolve_source_path(header_path, /):
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
    source_path = resolve_source_path(header_path)
    if source_path is None:
        source_path = pathlib.Path("hayward/server.c")

    args = CPP_ARGS[SOURCE_ROOT / source_path]
    return INDEX.parse(SOURCE_ROOT / header_path, args=args)


@functools.lru_cache(maxsize=None)
def parse_source_file(source_path):
    args = CPP_ARGS[SOURCE_ROOT / source_path]
    return INDEX.parse(SOURCE_ROOT / source_path, args=args)


class TestSourceMatchesHeader(unittest.TestCase):
    def test_source_matches_header_order(self):
        for source_path in enumerate_source_files():
            with self.subTest(file=source_path):
                header_path = resolve_header_path(source_path)
                if header_path is None:
                    self.skipTest(f"No header for {source_path}")

                header = parse_header_file(header_path)
                source = parse_source_file(source_path)

                header_decls = [
                    node.spelling
                    for node in header.cursor.get_children()
                    if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
                    and node.location.file.name == header.spelling
                ]
                source_defs = [
                    node.spelling
                    for node in source.cursor.get_children()
                    if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
                    and node.is_definition()
                    and node.storage_class != clang.cindex.StorageClass.STATIC
                    and node.location.file.name == source.spelling
                ]
                self.assertEqual(set(header_decls), set(source_defs))
                self.assertEqual(header_decls, source_defs)


if __name__ == "__main__":
    unittest.main(verbosity=2)
