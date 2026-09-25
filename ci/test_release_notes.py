"""Tests for the release-notes extractor.

The failure that matters is a quiet one: pulling the wrong section, or an empty
one, and publishing a release whose body says nothing. Each case below is a
shape the changelog actually takes.
"""

from __future__ import annotations

import importlib.util
import pathlib
import sys

import pytest

CI = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("release_notes", CI / "release_notes.py")
notes = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(notes)

CHANGELOG = """**English** | [Русский](CHANGELOG.ru.md)

# Changelog

## How this file works

Prose about the scheme.

---

## Unreleased

## 0.2.0

The newest one.

### Fixed

Something was broken.

---

## 0.1.2

The older one.

### Fixed

Something else.

## 0.1.1

Older still.
"""


def test_the_named_version_is_returned():
    body = notes.extract(CHANGELOG, "0.2.0")
    assert "The newest one." in body
    assert "Something was broken." in body


def test_a_section_stops_at_the_next_version():
    # The bug worth guarding: running past the rule into the version below and
    # publishing two releases' notes as one.
    body = notes.extract(CHANGELOG, "0.2.0")
    assert "The older one." not in body
    assert "0.1.2" not in body


def test_deeper_headings_stay_inside_the_section():
    body = notes.extract(CHANGELOG, "0.1.2")
    assert "### Fixed" in body
    assert "Something else." in body
    assert "Older still." not in body


def test_the_separating_rule_is_not_part_of_the_notes():
    body = notes.extract(CHANGELOG, "0.2.0")
    assert not body.rstrip().endswith("---")


def test_prose_headings_are_not_mistaken_for_versions():
    # "How this file works" is an H2 too; asking for it is asking for nothing.
    assert notes.extract(CHANGELOG, "How this file works") == ""


def test_an_unknown_version_yields_nothing():
    assert notes.extract(CHANGELOG, "9.9.9") == ""


def test_an_empty_section_yields_nothing():
    # Unreleased is empty here, and an empty body has to read as absent so the
    # caller can refuse rather than publish silence.
    assert notes.extract(CHANGELOG, "Unreleased") == ""


def test_the_leading_v_is_accepted_by_the_command_line(tmp_path, capsys, monkeypatch):
    path = tmp_path / "CHANGELOG.md"
    path.write_text(CHANGELOG, encoding="utf-8")
    monkeypatch.setattr(
        sys, "argv", ["release_notes.py", "v0.2.0", "--changelog", str(path)]
    )
    assert notes.main() == 0
    assert "The newest one." in capsys.readouterr().out


def test_a_missing_section_is_an_error_not_an_empty_release(
    tmp_path, capsys, monkeypatch
):
    path = tmp_path / "CHANGELOG.md"
    path.write_text(CHANGELOG, encoding="utf-8")
    monkeypatch.setattr(
        sys, "argv", ["release_notes.py", "3.0.0", "--changelog", str(path)]
    )
    assert notes.main() == 1
    assert "no section" in capsys.readouterr().err


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
