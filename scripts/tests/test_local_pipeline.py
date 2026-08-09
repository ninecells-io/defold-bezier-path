from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[2]


def load_script(name: str):
    path = REPO_ROOT / "scripts" / name
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


stage = load_script("stage_one_sudoku.py")
watch = load_script("watch.py")
bob = load_script("run_bob.py")


class DependencyRewriteTests(unittest.TestCase):
    def test_removes_local_release_and_renumbers_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "game.project"
            project.write_text(
                "[project]\n"
                "title = test\n"
                "dependencies#0 = https://example.test/first.zip\n"
                "dependencies#1 = https://github.com/ninecells-io/defold-bezier-path/archive/refs/tags/0.2.2.zip\n"
                "dependencies#2 = https://example.test/last.zip\n"
                "\n[display]\nwidth = 512\n"
            )

            stage.rewrite_dependencies(project)

            self.assertEqual(
                project.read_text(),
                "[project]\n"
                "title = test\n"
                "dependencies#0 = https://example.test/first.zip\n"
                "dependencies#1 = https://example.test/last.zip\n"
                "\n[display]\nwidth = 512\n",
            )

    def test_refuses_project_without_exactly_one_bezier_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "game.project"
            project.write_text(
                "[project]\n"
                "dependencies#0 = https://example.test/first.zip\n"
            )
            with self.assertRaises(SystemExit):
                stage.rewrite_dependencies(project)


class WatchSourceTests(unittest.TestCase):
    def create_tree(self, root: Path) -> None:
        (root / "bezierpath" / "src").mkdir(parents=True)
        (root / "bezierpath" / "include").mkdir(parents=True)
        (root / "bezierpath" / "src" / "a.cpp").write_text("int a = 1;\n")
        (root / "bezierpath" / "include" / "a.h").write_text("#pragma once\n")
        (root / "bezierpath" / "ext.manifest").write_text('name: "test"\n')

    def test_digest_changes_for_edits_additions_and_deletions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_tree(root)
            first = watch.source_digest(root)

            source = root / "bezierpath" / "src" / "a.cpp"
            source.write_text("int a = 2;\n")
            second = watch.source_digest(root)
            self.assertNotEqual(first, second)

            added = root / "bezierpath" / "include" / "b.h"
            added.write_text("#pragma once\n")
            third = watch.source_digest(root)
            self.assertNotEqual(second, third)

            added.unlink()
            self.assertEqual(second, watch.source_digest(root))


class BobAuthenticationTests(unittest.TestCase):
    PROJECT = (
        "[project]\n"
        "dependencies#0 = https://user:__GITHUB_TOKEN__@github.com/private/repo.zip\n"
        "dependencies#1 = https://github.com/public/repo.zip\n"
    )

    def test_requires_token_for_placeholder(self):
        with self.assertRaises(ValueError):
            bob.dependency_settings(self.PROJECT, None)

    def test_renders_settings_and_redacts_raw_and_encoded_token(self):
        settings, secrets = bob.dependency_settings(self.PROJECT, "secret/value")
        self.assertIsNotNone(settings)
        self.assertIn("secret%2Fvalue", settings)
        self.assertNotIn("__GITHUB_TOKEN__", settings)
        output = bob.redact("secret/value secret%2Fvalue", secrets)
        self.assertEqual("<redacted> <redacted>", output)

    def test_reads_token_from_dotenv(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / ".env").write_text(
                "# local credentials\n"
                "export GITHUB_TOKEN='dotenv token/value' # comment\n"
            )
            with patch.dict(os.environ, {}, clear=True):
                self.assertEqual("dotenv token/value", bob.github_token(root))

    def test_environment_token_takes_precedence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / ".env").write_text("GITHUB_TOKEN=dotenv-token\n")
            with patch.dict(os.environ, {"GITHUB_TOKEN": "environment-token"}, clear=True):
                self.assertEqual("environment-token", bob.github_token(root))

    def test_rejects_invalid_dotenv_syntax(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / ".env"
            path.write_text("not an assignment\n")
            with self.assertRaises(ValueError):
                bob.dotenv_values(path)


if __name__ == "__main__":
    unittest.main()
