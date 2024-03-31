"""
Checks that all source and header files directly import the headers that they
need.
"""
import difflib
import itertools
import re
import sys

from hayward_lint import PROJECT_ROOT, enumerate_source_paths


def test():
    passed = True

    expected_heading = [
        "#define _XOPEN_SOURCE 700\n",
        "#define _POSIX_C_SOURCE 200809L\n",
        "\n",
        "#include <config.h>\n",
        "\n",
    ]

    for source_path in itertools.chain(enumerate_source_paths()):
        with source_path.open() as source_file:
            actual_heading = [
                source_file.readline(),
                source_file.readline(),
                source_file.readline(),
                source_file.readline(),
                source_file.readline(),
            ]

        if actual_heading != expected_heading:
            msg = "======================================================================\n"
            msg += f"FAIL: test_source_heading: {source_path.relative_to(PROJECT_ROOT)}\n"
            msg += "----------------------------------------------------------------------\n"
            msg += "Source headings do not match expected:\n"

            for diff_line in difflib.ndiff(expected_heading, actual_heading):
                if diff_line.startswith("?"):
                    continue
                msg += f"  {diff_line}"
            msg += "\n"

            print(msg, file=sys.stderr)
            passed = False

    return passed


if __name__ == "__main__":
    sys.exit(0 if test() else 1)
