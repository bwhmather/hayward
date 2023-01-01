"""
Checks that all source and header files directly import the headers that they
need.
"""
import itertools
import pathlib
import sys
import traceback

from hayward_lint import (
    INCLUDE_ROOT,
    PROJECT_ROOT,
    SOURCE_ROOT,
    derive_include_from_path,
    enumerate_header_paths,
    enumerate_source_paths,
    read_ast_from_path,
    read_includes_from_path,
    resolve_clang_path,
    walk_file_preorder,
)

INCLUDE_ALIASES = {
    "json_types.h": "json.h",
    "gobject/gobject.h": "glib-object.h",
    "json_tokener.h": "json.h",
    "json_object.h": "json.h",
    "bits/stdint-uintn.h": "stdint.h",
    "bits/stdint-intn.h": "stdint.h",
    "bits/types/struct_timespec.h": "time.h",
    "bits/mathcalls.h": "math.h",
    "asm-generic/errno-base.h": "errno.h",
    "asm-generic/ioctls.h": "sys/ioctl.h",
    "bits/fcntl-linux.h": "fcntl.h",
    "bits/getopt_core.h": "getopt.h",
    "bits/getopt_ext.h": "getopt.h",
    "bits/resource.h": "sys/resource.h",
    "bits/sigaction.h": "signal.h",
    "bits/signum-generic.h": "signal.h",
    "bits/socket.h": "sys/socket.h",
    "bits/socket_type.h": "sys/socket.h",
    "bits/struct_stat.h": "sys/socket.h",
    "bits/time.h": "time.h",
    "bits/types/FILE.h": "stdio.h",
    "bits/types/clockid_t.h": "sys/types.h",
    "bits/types/sigset_t.h": "sys/types.h",
    "pango/pango-attributes.h": "pango/pango.h",
    "pango/pango-bidi-type.h": "pango/pango.h",
    "pango/pango-break.h": "pango/pango.h",
    "pango/pango-color.h": "pango/pango.h",
    "pango/pango-context.h": "pango/pango.h",
    "pango/pango-coverage.h": "pango/pango.h",
    "pango/pango-direction.h": "pango/pango.h",
    "pango/pango-engine.h": "pango/pango.h",
    "pango/pango-enum-types.h": "pango/pango.h",
    "pango/pango-features.h": "pango/pango.h",
    "pango/pango-font.h": "pango/pango.h",
    "pango/pango-fontmap.h": "pango/pango.h",
    "pango/pango-fontset.h": "pango/pango.h",
    "pango/pango-fontset-simple.h": "pango/pango.h",
    "pango/pango-glyph.h": "pango/pango.h",
    "pango/pango-glyph-item.h": "pango/pango.h",
    "pango/pango-gravity.h": "pango/pango.h",
    "pango/pango-item.h": "pango/pango.h",
    "pango/pango-language.h": "pango/pango.h",
    "pango/pango-layout.h": "pango/pango.h",
    "pango/pango-matrix.h": "pango/pango.h",
    "pango/pango-markup.h": "pango/pango.h",
    "pango/pango-renderer.h": "pango/pango.h",
    "pango/pango-script.h": "pango/pango.h",
    "pango/pango-tabs.h": "pango/pango.h",
    "pango/pango-types.h": "pango/pango.h",
    "pango/pango-utils.h": "pango/pango.h",
    "pango/pango-version-macros.h": "pango/pango.h",
}

BUILTIN_SYMBOLS = {
    "ssize_t": "sys/types.h",
}

IGNORED_SYMBOLS = {"NULL", "__u32"}


def _check_path(source_path, /):
    includes = set(read_includes_from_path(source_path))

    unused = set(includes)
    unused.discard("config.h")
    if source_path.is_relative_to(INCLUDE_ROOT) and source_path.relative_to(
        INCLUDE_ROOT
    ) == pathlib.Path("input/cursor.h"):
        unused.discard("linux/input-event-codes.h")

    indirect = dict()

    source = read_ast_from_path(source_path)

    for node in walk_file_preorder(source):
        if not node.referenced:
            continue
        ref = node.referenced
        if ref.spelling in BUILTIN_SYMBOLS:
            ref_include = BUILTIN_SYMBOLS[ref.spelling]
        else:
            if ref.location.file is None:
                continue
            ref_path = resolve_clang_path(ref.location.file.name)

            if ref.spelling in IGNORED_SYMBOLS:
                continue

            if ref_path == resolve_clang_path(source.spelling):
                continue

            ref_include = derive_include_from_path(ref_path)

        ref_include = INCLUDE_ALIASES.get(ref_include, ref_include)
        if ref_include in includes:
            unused.discard(ref_include)
            continue

        indirect.setdefault(ref_include, set()).add(ref.spelling)

    msg = "Includes do not match requirements\n\n"

    if indirect:
        msg += "The following files were depended on indirectly:\n"
        for indirect_path, indirect_refs in sorted(indirect.items()):
            msg += f"  - {indirect_path} ({', '.join(indirect_refs)})\n"
        msg += "\n"

    if unused:
        msg += "The following includes were unused:\n"
        for unused_path in sorted(unused):
            msg += f"  - {unused_path}\n"
        msg += "\n"

    if indirect or unused:
        raise AssertionError(msg)


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
