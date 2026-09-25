#!/usr/bin/env python3
"""Check the translation table against the sources it claims to translate.

Translations are keyed by their English source text. That makes a missing entry
harmless -- the English is shown -- and makes a *stale* entry invisible, which
is the failure this script exists to catch: edit an English string anywhere in
the interface and its translation stops matching, silently, with no warning at
build time and nothing wrong on screen until somebody switches language.

So: every key in the table has to appear, verbatim, as a string literal
somewhere under src/. Anything else is a key that can never be looked up.

Also checked, because each has a silent failure mode of its own:

  * duplicate keys -- the second one is unreachable, and which of the two wins
    is a detail of how the index is built rather than anything a reader chose
  * a translation equal to its key -- almost always a half-finished
    copy-and-paste rather than a word that is the same in both languages
  * format specifiers -- a translation that drops a %s, or reorders two of them
    against the arguments they consume, is not a typo but a crash

Run:  python ci/check_translations.py
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SOURCES = REPO / "src"
TABLE = SOURCES / "common/core/I18nRu.cpp"

# A C++ string literal, allowing escaped quotes, plus any adjacent literals the
# compiler would concatenate into it.
LITERAL = r'"(?:[^"\\]|\\.)*"'
RUN = re.compile(LITERAL + r"(?:\s*" + LITERAL + r")*")
PIECE = re.compile(LITERAL)

# Only the specifiers this codebase actually uses. A wider pattern would start
# matching percent signs in prose, and "60%" appears in more than one string.
SPECIFIER = re.compile(r"%[-+ #0-9.]*(?:ll|z|h)?[sdfuxzp]")


def join(literal_run: str) -> str:
    """The string the compiler would build from one run of adjacent literals."""
    body = "".join(piece[1:-1] for piece in PIECE.findall(literal_run))
    return (
        body.replace("\\n", "\n")
        .replace("\\t", "\t")
        .replace('\\"', '"')
        .replace("\\\\", "\\")
    )


def table_pairs(text: str) -> list[tuple[str, str]]:
    """Every {english, russian} row, in the order they appear.

    Rows are found by taking the literal runs inside each brace pair rather
    than by parsing C++: the file is a flat initialiser list and nothing else,
    so a brace with two runs in it is a row.
    """
    pairs: list[tuple[str, str]] = []
    for row in re.finditer(r"\{(.*?)\},", text, re.S):
        runs = RUN.findall(row.group(1))
        if len(runs) != 2:
            continue
        pairs.append((join(runs[0]), join(runs[1])))
    return pairs


def literals_in_sources() -> set[str]:
    """Every string literal in the tree, except the table's own."""
    found: set[str] = set()
    for path in sorted(SOURCES.rglob("*")):
        if path.suffix not in (".cpp", ".h") or path == TABLE:
            continue
        text = path.read_text(encoding="utf-8")
        for run in RUN.finditer(text):
            found.add(join(run.group(0)))
    return found


def check() -> list[tuple[str, str]]:
    """Returns (severity, message) pairs, where severity is "error" or "warning".

    The split is by what the problem does at runtime, not by how untidy it is.

    A stale, empty, duplicated or copy-pasted translation degrades gracefully:
    Translate() returns the English it was handed when a key does not resolve,
    so the worst case is one sentence in the wrong language in a panel that
    otherwise works. Failing a build over that taxes every English string edit
    for a defect nobody can see, in a language the maintainer does not read.

    A format-specifier mismatch does not degrade. These strings reach printf
    and TextOut; a translation that drops a %s or reorders two against the
    arguments they consume reads a pointer that was never passed. That is a
    crash, and it still fails the build.
    """
    problems: list[tuple[str, str]] = []
    pairs = table_pairs(TABLE.read_text(encoding="utf-8"))
    if not pairs:
        # Not a translation problem: the checker cannot see what it is meant to
        # check, so it is not reporting anything trustworthy.
        return [("error", f"{TABLE.name}: no translation rows found -- has the file moved?")]

    known = literals_in_sources()

    seen: dict[str, int] = {}
    for index, (english, russian) in enumerate(pairs, 1):
        where = f"row {index}"

        if english in seen:
            problems.append((
                "warning",
                f"{where}: duplicate key, already at row {seen[english]}: {english[:60]!r}",
            ))
        seen[english] = index

        if not russian.strip():
            problems.append(("warning", f"{where}: empty translation for {english[:60]!r}"))
        elif russian == english:
            problems.append((
                "warning",
                f"{where}: translation is identical to the key: {english[:60]!r}",
            ))

        if english not in known:
            problems.append((
                "warning",
                f"{where}: key appears in no source file, so it can never be "
                f"looked up: {english[:70]!r}",
            ))

        source_specifiers = SPECIFIER.findall(english)
        target_specifiers = SPECIFIER.findall(russian)
        if source_specifiers != target_specifiers:
            problems.append((
                "error",
                f"{where}: format specifiers differ -- {source_specifiers} in the "
                f"key, {target_specifiers} in the translation: {english[:50]!r}",
            ))

    return problems


def table_name() -> str:
    """The table's path for display, repo-relative where that makes sense.

    relative_to raises when the table is not under the repository, which is the
    normal case in this script's own tests -- so main() could not be called
    from them at all until this stopped assuming.
    """
    try:
        return TABLE.relative_to(REPO).as_posix()
    except ValueError:
        return TABLE.name


def main() -> int:
    problems = check()
    errors = [message for severity, message in problems if severity == "error"]
    warnings = [message for severity, message in problems if severity == "warning"]
    name = table_name()

    # GitHub Actions renders ::warning:: in the run summary without failing the
    # job, so a stale translation is visible to whoever can fix it rather than
    # blocking whoever cannot.
    for message in warnings:
        print(f"::warning file={name}::{message}")
    for message in errors:
        print(f"::error file={name}::{message}")

    pairs = table_pairs(TABLE.read_text(encoding="utf-8"))
    if errors:
        print(f"FAIL {name}: {len(errors)} error(s), "
              f"{len(warnings)} warning(s)")
        return 1
    if warnings:
        print(f"ok   {name}: {len(pairs)} translations, "
              f"{len(warnings)} stale -- warned, not fatal")
        return 0
    print(
        f"ok   {name}: {len(pairs)} translations, all keys present"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
