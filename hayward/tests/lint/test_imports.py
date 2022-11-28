import json
import os
import pathlib
import shlex
import unittest

import clang.cindex

SOURCE_ROOT = pathlib.Path(os.environ["MESON_SOURCE_ROOT"])
BUILD_ROOT = pathlib.Path(os.environ["MESON_BUILD_ROOT"])

_raw_commands = json.loads((BUILD_ROOT / "compile_commands.json").read_text())
cpp_args = {}
for _command in _raw_commands:
    _path = (pathlib.Path(_command["directory"]) / _command["file"]).resolve()
    cpp_args[_path] = [
        arg
        for arg in shlex.split(_command["command"])
        if arg.startswith(("-I", "-D", "-std"))
    ]


class TestSourceMatchesHeader(unittest.TestCase):
    def test_source_matches_header(self):
        source_dir = SOURCE_ROOT / pathlib.Path("hayward")
        include_dir = SOURCE_ROOT / pathlib.Path("include/hayward")

        for source_file in source_dir.glob("**/*.c"):
            source_file = source_file.resolve()
            include_file = (
                include_dir
                / source_file.parent.relative_to(source_dir)
                / (source_file.stem + ".h")
            ).resolve()
            if not include_file.exists():
            	# TODO most source files should have a corresponding header.
            	continue

            with self.subTest(file=source_file.name):
                index = clang.cindex.Index.create()
                hdr = index.parse(include_file, args=cpp_args[source_file])
                src = index.parse(source_file, args=cpp_args[source_file])

                hdr_decls = [
                    node.spelling
                    for node in hdr.cursor.get_children()
                    if node.kind == clang.cindex.CursorKind.FUNCTION_DECL
                    and node.location.file.name == hdr.spelling
                ]
                src_defs = [
                    node.spelling
                    for node in src.cursor.get_children()
                    if node.is_definition()
                    and node.storage_class != clang.cindex.StorageClass.STATIC
                    and node.location.file.name == src.spelling
                ]
                self.assertEqual(set(hdr_decls), set(src_defs))
                self.assertEqual(hdr_decls, src_defs)


if __name__ == "__main__":
    unittest.main()
