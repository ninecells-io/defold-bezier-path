from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import sys
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
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


stage = load_script("stage_consumers.py")
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

    def test_refuses_project_with_duplicate_bezier_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "game.project"
            project.write_text(
                "[project]\n"
                "dependencies#0 = https://example.test/defold-bezier-path/archive/one.zip\n"
                "dependencies#1 = https://example.test/defold-bezier-path/archive/two.zip\n"
            )
            with self.assertRaises(SystemExit):
                stage.rewrite_dependencies(project)


class ConsumerConfigurationTests(unittest.TestCase):
    def test_default_consumers_use_sibling_roots_and_isolated_stages(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo_root = Path(temporary) / "defold-bezier-path"
            configs = stage.consumer_configs(repo_root, {})

            self.assertEqual(list(configs), list(stage.DEFAULT_CONSUMERS))
            self.assertEqual(
                configs["defold-controls"].root,
                (repo_root.parent / "defold-controls").resolve(),
            )
            self.assertEqual(
                configs["defold-sudoku-playboard"].root,
                (repo_root.parent / "defold-sudoku-playboard").resolve(),
            )
            self.assertEqual(
                configs["one-sudoku"].stage_root,
                (repo_root / ".local-build" / "one-sudoku-stage").resolve(),
            )

    def test_root_and_stage_environment_overrides_are_resolved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "repo"
            controls_root = root / "custom-controls"
            controls_stage = root / "custom-stage"
            configs = stage.consumer_configs(
                repo_root,
                {
                    "DEFOLD_CONTROLS_ROOT": str(controls_root),
                    "DEFOLD_CONTROLS_STAGE_ROOT": str(controls_stage),
                },
            )

            self.assertEqual(configs["defold-controls"].root, controls_root.resolve())
            self.assertEqual(
                configs["defold-controls"].stage_root,
                controls_stage.resolve(),
            )

    def test_selection_defaults_to_all_and_rejects_unknown_or_duplicates(self):
        with tempfile.TemporaryDirectory() as temporary:
            configs = stage.consumer_configs(Path(temporary) / "repo", {})
            selected = stage.select_consumers([], configs)
            self.assertEqual(
                [consumer.identifier for consumer in selected],
                list(stage.DEFAULT_CONSUMERS),
            )

            with self.assertRaises(SystemExit):
                stage.select_consumers(["unknown"], configs)
            with self.assertRaises(SystemExit):
                stage.select_consumers(["one-sudoku", "one-sudoku"], configs)


class ConsumerStagingTests(unittest.TestCase):
    def create_extension(self, repo_root: Path) -> Path:
        extension = repo_root / "bezierpath"
        (extension / "src").mkdir(parents=True)
        (extension / "include").mkdir(parents=True)
        (extension / "ext.manifest").write_text('name: "BezierPath"\n')
        (extension / "bezierpath.lua").write_text("return {}\n")
        (extension / "src" / "bezierpath.cpp").write_text("int value = 1;\n")
        (extension / "include" / "bezierpath.h").write_text("#pragma once\n")
        return extension

    def create_consumer(self, root: Path) -> None:
        root.mkdir(parents=True)
        (root / "game.project").write_text(
            "[project]\n"
            "dependencies#0 = https://example.test/first.zip\n"
            "dependencies#1 = https://example.test/defold-bezier-path/archive/release.zip\n"
            "dependencies#2 = https://example.test/last.zip\n"
        )
        (root / "working-copy.lua").write_text("return 'uncommitted'\n")
        (root / ".env").write_text("SECRET=do-not-stage\n")
        (root / ".git").mkdir()
        (root / ".git" / "config").write_text("local git state\n")
        (root / "build").mkdir()
        (root / "build" / "output").write_text("generated\n")
        (root / ".local-build").mkdir()
        (root / ".local-build" / "output").write_text("generated\n")
        (root / ".internal" / "lib").mkdir(parents=True)
        (root / ".internal" / "lib" / "cached.zip").write_bytes(b"cache")

    def test_stage_copies_working_tree_cache_and_complete_local_extension(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "defold-bezier-path"
            local_extension = self.create_extension(repo_root)
            consumer_root = root / "defold-controls"
            self.create_consumer(consumer_root)
            stage_root = root / "stage"
            (stage_root / ".internal" / "lib").mkdir(parents=True)
            (stage_root / ".internal" / "lib" / "stale.zip").write_bytes(b"stale")
            (stage_root / ".env").write_text("OLD_SECRET=remove-me\n")
            (stage_root / "build").mkdir()
            (stage_root / "build" / "old-output").write_text("stale\n")
            consumer = stage.Consumer("defold-controls", consumer_root, stage_root)

            stage.stage_consumer(consumer, repo_root, local_extension)

            self.assertTrue((stage_root / "working-copy.lua").is_file())
            self.assertFalse((stage_root / ".env").exists())
            self.assertFalse((stage_root / ".git").exists())
            self.assertFalse((stage_root / "build").exists())
            self.assertFalse((stage_root / ".local-build").exists())
            self.assertEqual(
                (stage_root / ".internal" / "lib" / "cached.zip").read_bytes(),
                b"cache",
            )
            self.assertFalse(
                (stage_root / ".internal" / "lib" / "stale.zip").exists()
            )
            self.assertNotIn(
                stage.RELEASE_DEPENDENCY_MARKER,
                (stage_root / "game.project").read_text(),
            )
            self.assertEqual(
                stage.extension_contents(stage_root / "bezierpath"),
                stage.extension_contents(local_extension),
            )

    def test_stage_refuses_missing_consumer_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "defold-bezier-path"
            local_extension = self.create_extension(repo_root)
            consumer = stage.Consumer(
                "defold-controls",
                root / "missing",
                root / "stage",
            )

            with self.assertRaises(SystemExit):
                stage.stage_consumer(consumer, repo_root, local_extension)

    def test_validation_detects_changed_native_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "defold-bezier-path"
            local_extension = self.create_extension(repo_root)
            consumer_root = root / "one-sudoku"
            self.create_consumer(consumer_root)
            stage_root = root / "stage"
            consumer = stage.Consumer("one-sudoku", consumer_root, stage_root)
            stage.stage_consumer(consumer, repo_root, local_extension)
            (stage_root / "bezierpath" / "src" / "bezierpath.cpp").write_text(
                "int value = 2;\n"
            )

            with self.assertRaises(SystemExit):
                stage.validate_stage(stage_root, local_extension)


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
