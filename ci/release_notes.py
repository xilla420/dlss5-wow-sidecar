#!/usr/bin/env python3
"""Lift one version's section out of CHANGELOG.md.

The release notes are already written -- in the changelog, in the pull request
that made each change, while the reason was still fresh. Typing them a second
time into the GitHub release form is how the two drift apart, and the one people
read is whichever they happen to find first.

So the workflow reads them from here instead, and fails when the section is
missing rather than publishing a release with an empty body.

    python ci/release_notes.py 0.2.0
    python ci/release_notes.py v0.2.0 --changelog CHANGELOG.md
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# "## 0.2.0", and nothing else on the line. The header level matters: the
# document's own headings inside a section are deeper, and matching them would
# end the section early.
HEADING = re.compile(r"^##\s+(?!\[)(?P<version>[0-9]+\.[0-9]+\.[0-9]+)\s*$")
ANY_H2 = re.compile(r"^##\s+")


def extract(text: str, version: str) -> str:
    """The body under `## <version>`, up to the next second-level heading."""
    lines = text.splitlines()
    start = None
    for index, line in enumerate(lines):
        match = HEADING.match(line)
        if match and match.group("version") == version:
            start = index + 1
            break
    if start is None:
        return ""

    body = []
    for line in lines[start:]:
        if ANY_H2.match(line):
            break
        body.append(line)

    # A horizontal rule separates versions in this file and is not part of
    # either of them.
    while body and body[-1].strip() in ("", "---"):
        body.pop()
    while body and not body[0].strip():
        body.pop(0)
    return "\n".join(body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="0.2.0 or v0.2.0")
    parser.add_argument("--changelog", default=str(REPO / "CHANGELOG.md"))
    args = parser.parse_args()

    version = args.version.lstrip("vV")
    path = pathlib.Path(args.changelog)
    if not path.exists():
        print(f"no changelog at {path}", file=sys.stderr)
        return 1

    body = extract(path.read_text(encoding="utf-8"), version)
    if not body.strip():
        print(
            f"CHANGELOG.md has no section for {version}. Add one before tagging: "
            f"a release whose notes are empty is a release nobody can read.",
            file=sys.stderr,
        )
        return 1

    # The notes are UTF-8: they carry em dashes, and one entry quotes a Cyrillic
    # path. Windows hands stdout the ANSI code page by default, which raises on
    # both, so the stream is told what it is about to carry.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stdout.write(body + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
