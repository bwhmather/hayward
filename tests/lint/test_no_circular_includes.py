import sys

from hayward_lint import (
    INCLUDE_ROOT,
    derive_include_from_path,
    enumerate_header_paths,
    read_includes_from_path,
    resolve_include_path,
)


def test():
    # Build dependency graph.
    graph = {}
    for header_path in enumerate_header_paths():
        graph[derive_include_from_path(header_path)] = {
            dep_path
            for dep_path in read_includes_from_path(header_path)
            if resolve_include_path(dep_path).is_relative_to(INCLUDE_ROOT)
        }

    # Check for cycles.
    for root in graph:
        queue = {root}
        visited = set()
        while queue:
            dep = queue.pop()
            visited.add(dep)
            deps = graph[dep]

            if root in deps:
                msg = "======================================================================\n"
                msg += "FAIL: test_no_circular_includes\n"
                msg += "----------------------------------------------------------------------\n"
                msg += "The following headers form a cycle:\n"
                for dep in deps:
                    msg += f"  - {dep}\n"
                msg += "\n"

                print(msg, file=sys.stderr)
                return False

            queue.update(deps.difference(visited))

    return True


if __name__ == "__main__":
    sys.exit(0 if test() else 1)
