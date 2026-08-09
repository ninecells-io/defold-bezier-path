from __future__ import annotations

import importlib.util
import io
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request
import zipfile


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
dependency_server = load_script("dependency_server.py")


class DependencyRewriteTests(unittest.TestCase):
    LOCAL_URL = "http://127.0.0.1:9012"

    def test_rewrites_first_party_dependencies_and_preserves_indexes(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "game.project"
            project.write_text(
                "[project]\n"
                "title = test\n"
                "dependencies#2 = https://example.test/first.zip\n"
                "dependencies#4 = https://github.com/ninecells-io/defold-bezier-path/archive/refs/tags/0.2.2.zip\n"
                "dependencies#7 = https://github.com/ninecells-io/defold-helpers/archive/refs/tags/0.1.5.zip\n"
                "\n[display]\nwidth = 512\n"
            )

            stage.rewrite_dependencies(
                project,
                {"defold-bezier-path", "defold-helpers"},
                self.LOCAL_URL,
            )

            self.assertEqual(
                project.read_text(),
                "[project]\n"
                "title = test\n"
                "dependencies#2 = https://example.test/first.zip\n"
                "dependencies#4 = http://127.0.0.1:9012/defold-bezier-path.zip\n"
                "dependencies#7 = http://127.0.0.1:9012/defold-helpers.zip\n"
                "\n[display]\nwidth = 512\n",
            )

    def test_accepts_already_local_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "game.project"
            project.write_text(
                "[project]\n"
                f"dependencies#0 = {self.LOCAL_URL}/defold-bezier-path.zip\n"
            )
            original = project.read_text()
            stage.rewrite_dependencies(
                project, {"defold-bezier-path"}, self.LOCAL_URL
            )
            self.assertEqual(original, project.read_text())

    def test_refuses_missing_or_duplicate_first_party_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "game.project"
            project.write_text(
                "[project]\n"
                "dependencies#0 = https://example.test/first.zip\n"
            )
            with self.assertRaises(SystemExit):
                stage.rewrite_dependencies(
                    project, {"defold-bezier-path"}, self.LOCAL_URL
                )
            project.write_text(
                "[project]\n"
                "dependencies#0 = https://github.com/ninecells-io/defold-bezier-path/archive/one.zip\n"
                "dependencies#1 = https://github.com/ninecells-io/defold-bezier-path/archive/two.zip\n"
            )
            with self.assertRaises(SystemExit):
                stage.rewrite_dependencies(
                    project, {"defold-bezier-path"}, self.LOCAL_URL
                )


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
    def create_consumer(self, root: Path) -> None:
        root.mkdir(parents=True)
        (root / "game.project").write_text(
            "[project]\n"
            "dependencies#0 = https://github.com/ninecells-io/defold-sudoku-playboard/archive/release.zip\n"
            "dependencies#1 = https://github.com/ninecells-io/defold-controls/archive/release.zip\n"
            "dependencies#2 = https://github.com/ninecells-io/defold-bezier-path/archive/release.zip\n"
            "dependencies#3 = https://github.com/ninecells-io/defold-helpers/archive/release.zip\n"
            "dependencies#4 = https://example.test/third-party.zip\n"
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

    def test_stage_copies_working_tree_cache_and_rewrites_first_party_urls(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "defold-bezier-path"
            repo_root.mkdir()
            consumer_root = root / "one-sudoku"
            self.create_consumer(consumer_root)
            stage_root = root / "stage"
            (stage_root / ".internal" / "lib").mkdir(parents=True)
            (stage_root / ".internal" / "lib" / "stale.zip").write_bytes(b"stale")
            (stage_root / ".env").write_text("OLD_SECRET=remove-me\n")
            (stage_root / "build").mkdir()
            (stage_root / "build" / "old-output").write_text("stale\n")
            consumer = stage.Consumer("one-sudoku", consumer_root, stage_root)

            stage.stage_consumer(consumer, repo_root, "http://127.0.0.1:9012")

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
            project_text = (stage_root / "game.project").read_text()
            for package in stage.FIRST_PARTY_PACKAGES:
                self.assertIn(
                    f"http://127.0.0.1:9012/{package}.zip", project_text
                )
            self.assertIn("https://example.test/third-party.zip", project_text)

    def test_stage_refuses_missing_consumer_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "defold-bezier-path"
            consumer = stage.Consumer(
                "defold-controls",
                root / "missing",
                root / "stage",
            )

            with self.assertRaises(SystemExit):
                stage.stage_consumer(consumer, repo_root, "http://127.0.0.1:9012")

    def test_validation_detects_changed_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "defold-bezier-path"
            repo_root.mkdir()
            consumer_root = root / "one-sudoku"
            self.create_consumer(consumer_root)
            stage_root = root / "stage"
            consumer = stage.Consumer("one-sudoku", consumer_root, stage_root)
            stage.stage_consumer(consumer, repo_root, "http://127.0.0.1:9012")
            project_file = stage_root / "game.project"
            project_file.write_text(
                project_file.read_text().replace(
                    "http://127.0.0.1:9012/defold-helpers.zip",
                    "https://example.test/defold-helpers/archive/release.zip",
                )
            )

            with self.assertRaises(SystemExit):
                stage.validate_stage(
                    stage_root,
                    set(stage.FIRST_PARTY_PACKAGES),
                    "http://127.0.0.1:9012",
                )


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

    def test_digest_includes_all_exported_sibling_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "defold-bezier-path"
            self.create_tree(repo_root)
            controls_root = root / "controls-repo"
            playboard_root = root / "playboard-repo"
            helpers_root = root / "helpers-repo"
            (controls_root / "controls").mkdir(parents=True)
            (playboard_root / "sudoku").mkdir(parents=True)
            (helpers_root / "helpers").mkdir(parents=True)
            (controls_root / "controls" / "control.lua").write_text("return 1\n")
            (playboard_root / "sudoku" / "sudoku.lua").write_text("return 1\n")
            helper = helpers_root / "helpers" / "helper.lua"
            helper.write_text("return 1\n")
            environment = {
                "DEFOLD_CONTROLS_ROOT": str(controls_root),
                "DEFOLD_SUDOKU_PLAYBOARD_ROOT": str(playboard_root),
                "DEFOLD_HELPERS_ROOT": str(helpers_root),
            }
            first = watch.source_digest(repo_root, environment)
            helper.write_text("return 2\n")
            self.assertNotEqual(first, watch.source_digest(repo_root, environment))


class DependencyServerTests(unittest.TestCase):
    def create_package(self, root: Path) -> object:
        root.mkdir(parents=True)
        (root / "game.project").write_text(
            "[project]\n"
            "title = example\n"
            "dependencies#0 = https://user:secret@example.test/private.zip\n\n"
            "[library]\n"
            "include_dirs = library\n"
        )
        (root / "library" / "nested").mkdir(parents=True)
        (root / "library" / "module.lua").write_text("return 1\n")
        (root / "library" / "nested" / "asset.txt").write_text("asset\n")
        (root / "library" / "tests").mkdir()
        (root / "library" / "tests" / "ignored.lua").write_text("ignored\n")
        (root / "library" / ".env").write_text("SECRET=value\n")
        return dependency_server.Package("example", root)

    def test_archive_is_valid_minimal_deterministic_and_content_addressed(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = self.create_package(Path(temporary) / "example")
            first_payload, first_etag = dependency_server.build_archive(package)
            second_payload, second_etag = dependency_server.build_archive(package)
            self.assertEqual(first_payload, second_payload)
            self.assertEqual(first_etag, second_etag)
            with zipfile.ZipFile(io.BytesIO(first_payload)) as archive:
                self.assertEqual(
                    sorted(name for name in archive.namelist() if not name.endswith("/")),
                    [
                        "game.project",
                        "library/module.lua",
                        "library/nested/asset.txt",
                    ],
                )
                self.assertIn("library/nested/", archive.namelist())
                manifest = archive.read("game.project").decode()
                self.assertIn("include_dirs = library", manifest)
                self.assertNotIn("secret", manifest)
            (package.root / "library" / "module.lua").write_text("return 2\n")
            changed_payload, changed_etag = dependency_server.build_archive(package)
            self.assertNotEqual(first_payload, changed_payload)
            self.assertNotEqual(first_etag, changed_etag)

    def test_missing_project_or_include_directory_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = dependency_server.Package("missing", Path(temporary) / "missing")
            with self.assertRaises(ValueError):
                dependency_server.include_directories(package)

    def test_http_etag_conditional_request_health_and_not_found(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = self.create_package(Path(temporary) / "example")
            try:
                server = dependency_server.ThreadingHTTPServer(
                    ("127.0.0.1", 0), dependency_server.DependencyRequestHandler
                )
            except PermissionError:
                self.skipTest("sandbox does not permit binding a loopback socket")
            server.packages = {"example": package}
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base_url = f"http://127.0.0.1:{server.server_address[1]}"
            try:
                with urllib.request.urlopen(f"{base_url}/health") as response:
                    self.assertEqual(200, response.status)
                with urllib.request.urlopen(f"{base_url}/example.zip") as response:
                    etag = response.headers["ETag"]
                    self.assertEqual("application/zip", response.headers["Content-Type"])
                request = urllib.request.Request(
                    f"{base_url}/example.zip", headers={"If-None-Match": etag}
                )
                with self.assertRaises(urllib.error.HTTPError) as not_modified:
                    urllib.request.urlopen(request)
                self.assertEqual(304, not_modified.exception.code)
                with self.assertRaises(urllib.error.HTTPError) as missing:
                    urllib.request.urlopen(f"{base_url}/missing.zip")
                self.assertEqual(404, missing.exception.code)
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)


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

    def test_renders_production_settings_without_changing_other_values(self):
        source = (
            "[project]\n"
            "dependencies#0 = https://user:__GITHUB_TOKEN__@example.test/release.zip\n"
            "dependencies#4 = https://example.test/third-party.zip\n"
        )
        rendered, secrets = bob.render_placeholders(source, "secret/value", "test")
        self.assertIn("secret%2Fvalue", rendered)
        self.assertIn("dependencies#4 = https://example.test/third-party.zip", rendered)
        self.assertEqual(["secret/value", "secret%2Fvalue"], secrets)

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
