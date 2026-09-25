"""Tests for the translation checker.

A checker nobody tests is a checker that passes everything. Each case below
breaks the table in one specific way and asserts the tool notices -- the point
being that a green run means something, not that the script ran.
"""

from __future__ import annotations

import importlib.util
import pathlib
import sys

import pytest

CI = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "check_translations", CI / "check_translations.py"
)
checker = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(checker)


def build(tmp_path: pathlib.Path, rows: str, sources: str) -> None:
    """Point the checker at a throwaway tree with one table and one source."""
    src = tmp_path / "src"
    (src / "common" / "core").mkdir(parents=True)
    table = src / "common" / "core" / "I18nRu.cpp"
    table.write_text(
        "namespace sidecar {\n"
        "const TranslationPair kRussianTable[] = {\n" + rows + "};\n}\n",
        encoding="utf-8",
    )
    (src / "ui.cpp").write_text(sources, encoding="utf-8")
    checker.SOURCES = src
    checker.TABLE = table


def test_a_table_whose_keys_all_exist_passes(tmp_path):
    build(
        tmp_path,
        '    {"Detect", "Найти"},\n',
        'ImGui::Button(Tr("Detect"));\n',
    )
    assert checker.check() == []


def test_a_key_no_source_uses_is_reported(tmp_path):
    # The failure this tool exists for: someone edits the English and the
    # translation stops matching, with nothing on screen to show for it.
    build(
        tmp_path,
        '    {"Detect it", "Найти"},\n',
        'ImGui::Button(Tr("Detect"));\n',
    )
    problems = checker.check()
    assert len(problems) == 1
    assert "no source file" in problems[0]


def test_a_duplicate_key_is_reported(tmp_path):
    # The second row is unreachable, and which one wins is an accident of how
    # the index happens to be built.
    build(
        tmp_path,
        '    {"Detect", "Найти"},\n    {"Detect", "Определить"},\n',
        'Tr("Detect");\n',
    )
    problems = checker.check()
    assert any("duplicate key" in p for p in problems)


def test_a_translation_equal_to_its_key_is_reported(tmp_path):
    build(tmp_path, '    {"Detect", "Detect"},\n', 'Tr("Detect");\n')
    problems = checker.check()
    assert any("identical to the key" in p for p in problems)


def test_an_empty_translation_is_reported(tmp_path):
    # Worse than no entry at all: no entry shows English, this shows nothing.
    build(tmp_path, '    {"Detect", ""},\n', 'Tr("Detect");\n')
    problems = checker.check()
    assert any("empty translation" in p for p in problems)


def test_a_dropped_format_specifier_is_reported(tmp_path):
    # Not cosmetic: the call still passes an argument the format no longer
    # consumes.
    build(
        tmp_path,
        '    {"%llu presented, %llu dropped", "%llu показано"},\n',
        'Tr("%llu presented, %llu dropped");\n',
    )
    problems = checker.check()
    assert any("format specifiers differ" in p for p in problems)


def test_reordered_specifiers_of_different_types_are_reported(tmp_path):
    build(
        tmp_path,
        '    {"Wanted as %s, size %zu", "Размер %zu, нужен как %s"},\n',
        'Tr("Wanted as %s, size %zu");\n',
    )
    problems = checker.check()
    assert any("format specifiers differ" in p for p in problems)


def test_a_percent_in_prose_is_not_mistaken_for_a_specifier(tmp_path):
    # "mixed in at 60%" appears in the real table, and an over-eager pattern
    # would fail the whole file over it.
    build(
        tmp_path,
        '    {"mixed in at 60% here", "подмешан на 60% здесь"},\n',
        'Tr("mixed in at 60% here");\n',
    )
    assert checker.check() == []


def test_adjacent_literals_are_joined_the_way_the_compiler_joins_them(tmp_path):
    # Long strings are split across lines in both the table and the sources,
    # and split at different points. Comparing them raw would never match.
    build(
        tmp_path,
        '    {"one "\n     "two", "раз два"},\n',
        'Hint("one two");\n',
    )
    assert checker.check() == []


def test_escapes_are_resolved_before_comparing(tmp_path):
    build(
        tmp_path,
        '    {"first\\nsecond", "первая\\nвторая"},\n',
        'Tr("first\\nsecond");\n',
    )
    assert checker.check() == []


def test_a_table_with_no_rows_is_reported_rather_than_passing(tmp_path):
    # An empty table trivially satisfies every other check, so silence here
    # would be the checker's own worst failure mode.
    build(tmp_path, "", 'Tr("Detect");\n')
    problems = checker.check()
    assert len(problems) == 1
    assert "no translation rows" in problems[0]


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
