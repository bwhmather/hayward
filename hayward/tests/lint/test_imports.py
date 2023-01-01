import functools
import itertools
import json
import os
import pathlib
import re
import shlex
import subprocess
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


DEFAULT_INCLUDE_DIRS = [
    pathlib.Path(match.group(1))
    for match in re.finditer(
        "^ (.*)$",
        subprocess.run(
            ["clang", "-xc", "/dev/null", "-E", "-Wp,-v"],
            capture_output=True,
            encoding="utf-8",
        ).stderr,
        flags=re.MULTILINE,
    )
]

INDEX = clang.cindex.Index.create()


def enumerate_source_paths():
    paths = [source_path for source_path in HAYWARD_ROOT.glob("**/*.c")]
    yield from sorted(paths)


def enumerate_header_paths():
    paths = [header_path for header_path in HAYWARD_INCLUDE_ROOT.glob("**/*.h")]
    yield from sorted(paths)


def is_source_path(path, /):
    assert path.is_absolute()

    return path.suffix == ".c"


def is_header_path(path, /):
    assert path.is_absolute()

    return path.suffix == ".h"


def is_hayward_source_path(path, /):
    assert path.is_absolute()

    if not is_source_path(path):
        return False

    if not path.is_relative_to(HAYWARD_ROOT):
        return False

    return True


def is_hayward_header_path(path, /):
    assert path.is_absolute()

    if not is_header_path(path):
        return False

    if not path.is_relative_to(HAYWARD_INCLUDE_ROOT):
        return False

    return True


def header_path_for_source_path(source_path, /):
    assert is_source_path(source_path)
    assert source_path.is_relative_to(HAYWARD_ROOT)

    header_path = (
        HAYWARD_INCLUDE_ROOT
        / source_path.relative_to(HAYWARD_ROOT).parent
        / (source_path.stem + ".h")
    )

    if not header_path.exists():
        return None

    return header_path


def source_path_for_header_path(header_path, /):
    assert is_header_path(header_path)
    assert header_path.is_relative_to(HAYWARD_INCLUDE_ROOT)

    source_path = (
        HAYWARD_ROOT
        / header_path.relative_to(HAYWARD_INCLUDE_ROOT).parent
        / (header_path.stem + ".c")
    )

    if not source_path.exists():
        return None

    return source_path


def get_args(path, /):
    assert path.is_absolute()
    assert path.is_relative_to(SOURCE_ROOT)

    if is_header_path(path):
        source_path = source_path_for_header_path(path)
        if source_path is None:
            source_path = HAYWARD_ROOT / "server.c"
        args = CPP_ARGS[source_path]
    else:
        args = CPP_ARGS[path]

    args = [*args, *(f"-I{sys_path}" for sys_path in DEFAULT_INCLUDE_DIRS)]
    return args


@functools.lru_cache(maxsize=None)
def read_ast_from_path(path, /):
    return INDEX.parse(
        path,
        args=get_args(path),
        options=clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
    )


def resolve_clang_path(path):
    path = pathlib.Path(path)
    if not path.is_absolute():
        path = BUILD_ROOT / path
    path = path.resolve()
    return path


@functools.lru_cache(maxsize=None)
def include_dirs(source_path):
    return [
        (BUILD_ROOT / pathlib.Path(arg[2:])).resolve()
        for arg in get_args(source_path)
        if arg.startswith("-I")
    ]


@functools.lru_cache(maxsize=None)
def resolve_include_path(include_path, /, *, source_path=None):
    if source_path is None:
        source_path = HAYWARD_ROOT / "server.c"

    assert source_path.is_absolute()
    assert isinstance(include_path, str)

    for candidate_dir in include_dirs(source_path):
        candidate_path = candidate_dir / include_path
        candidate_path = candidate_path.resolve()
        if candidate_path.exists():
            return candidate_path

    raise Exception(f"Could not resolve {include_path}")


@functools.lru_cache(maxsize=None)
def derive_include_from_path(include_path, /):
    assert include_path.is_absolute()

    best_include = str(include_path)

    for candidate_dir in include_dirs(HAYWARD_ROOT / "server.c"):
        if not include_path.is_relative_to(candidate_dir):
            continue

        candidate_include = str(include_path.relative_to(candidate_dir))
        if len(candidate_include) >= len(best_include):
            continue

        best_include = candidate_include

    return best_include


@functools.lru_cache(maxsize=None)
def _read_includes_from_path(path, /):
    pattern = re.compile('^#include ["<](.*)[>"]$', flags=re.MULTILINE)

    source = path.read_text()
    return [match.group(1) for match in pattern.finditer(source)]


def read_includes_from_path(path, /):
    # Clang entirely removes files with an include guard or `#pragma once` from
    # the tree when they are included a second time.  This makes the clang ast
    # useless for finding direct includes and we need to reparse.
    yield from _read_includes_from_path(path)


def same_clang_path(a, b):
    return resolve_clang_path(a) == resolve_clang_path(b)


def _walk_preorder(cursor, /, *, root_path):
    yield cursor
    for child in cursor.get_children():
        if child.location.file is None:
            continue

        if resolve_clang_path(child.location.file.name) != root_path:
            continue

        for descendant in _walk_preorder(child, root_path=root_path):
            yield descendant


def walk_file_preorder(source, /):
    root_path = resolve_clang_path(source.spelling)
    yield from _walk_preorder(source.cursor, root_path=root_path)


class DeclarationOrderTestCase(unittest.TestCase):
    maxDiff = 2000

    def test_source_and_header_orders_match(self):
        for source_path in enumerate_source_paths():
            with self.subTest(file=source_path):
                if source_path.match("hayward/src/commands.c"):
                    self.skipTest("Command modules share a single header")

                if source_path.is_relative_to("hayward/src/commands"):
                    self.skipTest("Command modules share a single header")

                header_path = header_path_for_source_path(source_path)
                if header_path is None:
                    self.skipTest(f"No header for {source_path}")

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

                self.assertEqual(set(header_decls), set(source_defs))
                self.assertEqual("\n".join(header_decls), "\n".join(source_defs))


if __name__ == "__main__":
    unittest.main(verbosity=2)
