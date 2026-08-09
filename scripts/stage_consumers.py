#!/usr/bin/env python3
"""Create isolated consumer build roots using local first-party libraries."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Mapping, NoReturn, Sequence
from urllib.parse import urlparse


FIRST_PARTY_PACKAGES = (
    "defold-sudoku-playboard",
    "defold-controls",
    "defold-bezier-path",
    "defold-helpers",
)
EXPECTED_PACKAGES = {
    "defold-controls": {"defold-bezier-path", "defold-helpers"},
    "defold-sudoku-playboard": {"defold-bezier-path", "defold-helpers"},
    "one-sudoku": set(FIRST_PARTY_PACKAGES),
}
DEFAULT_CONSUMERS = (
    "defold-controls",
    "defold-sudoku-playboard",
    "one-sudoku",
)


@dataclass(frozen=True)
class Consumer:
    identifier: str
    root: Path
    stage_root: Path


def fail(message: str) -> NoReturn:
    raise SystemExit(f"stage error: {message}")


def consumer_configs(
    repo_root: Path,
    environment: Mapping[str, str] | None = None,
) -> dict[str, Consumer]:
    values = os.environ if environment is None else environment
    local_build_root = Path(
        values.get("LOCAL_BUILD_ROOT", str(repo_root / ".local-build"))
    ).resolve()
    definitions = (
        (
            "defold-controls",
            "DEFOLD_CONTROLS_ROOT",
            repo_root.parent / "defold-controls",
            "DEFOLD_CONTROLS_STAGE_ROOT",
            local_build_root / "defold-controls-stage",
        ),
        (
            "defold-sudoku-playboard",
            "DEFOLD_SUDOKU_PLAYBOARD_ROOT",
            repo_root.parent / "defold-sudoku-playboard",
            "DEFOLD_SUDOKU_PLAYBOARD_STAGE_ROOT",
            local_build_root / "defold-sudoku-playboard-stage",
        ),
        (
            "one-sudoku",
            "ONE_SUDOKU_ROOT",
            repo_root.parent / "one-sudoku",
            "APP_STAGE_ROOT",
            local_build_root / "one-sudoku-stage",
        ),
    )

    return {
        identifier: Consumer(
            identifier=identifier,
            root=Path(values.get(root_key, str(default_root))).resolve(),
            stage_root=Path(values.get(stage_key, str(default_stage))).resolve(),
        )
        for identifier, root_key, default_root, stage_key, default_stage in definitions
    }


def select_consumers(
    names: Sequence[str],
    configs: Mapping[str, Consumer],
) -> list[Consumer]:
    selected_names = list(names) if names else list(DEFAULT_CONSUMERS)
    duplicate_names = sorted(
        name for name in set(selected_names) if selected_names.count(name) > 1
    )
    if duplicate_names:
        fail(f"consumer selected more than once: {', '.join(duplicate_names)}")

    unknown = [name for name in selected_names if name not in configs]
    if unknown:
        fail(
            f"unknown consumer {unknown[0]!r}; expected one of "
            + ", ".join(DEFAULT_CONSUMERS)
        )
    return [configs[name] for name in selected_names]


def run_rsync(source: Path, destination: Path, *extra: str) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    command = ["rsync", "-a", *extra, f"{source}/", f"{destination}/"]
    subprocess.run(command, check=True)


def first_party_package(value: str) -> str | None:
    parsed = urlparse(value)
    for package in FIRST_PARTY_PACKAGES:
        if (
            parsed.scheme in ("http", "https")
            and parsed.hostname in ("127.0.0.1", "localhost")
            and parsed.path == f"/{package}.zip"
        ):
            return package
        if (
            parsed.hostname == "github.com"
            and parsed.path.startswith(f"/ninecells-io/{package}/archive/")
            and parsed.path.endswith(".zip")
        ):
            return package
    return None


def rewrite_dependencies(
    project_file: Path,
    expected_packages: set[str],
    local_dependency_url: str,
) -> None:
    lines = project_file.read_text().splitlines(keepends=True)
    dependency = re.compile(r"^(\s*)dependencies#(\d+)(\s*=\s*)(.*?)(\r?\n)?$")
    in_project = False
    found = 0
    matched: list[str] = []
    rewritten: list[str] = []

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_project = stripped == "[project]"

        match = dependency.match(line) if in_project else None
        if not match:
            rewritten.append(line)
            continue

        found += 1
        value = match.group(4)
        package = first_party_package(value)
        if package is not None:
            matched.append(package)
            value = f"{local_dependency_url.rstrip('/')}/{package}.zip"

        newline = match.group(5) or ""
        rewritten.append(
            f"{match.group(1)}dependencies#{match.group(2)}"
            f"{match.group(3)}{value}{newline}"
        )

    if found == 0:
        fail(f"no project dependencies found in {project_file}")
    duplicates = sorted(package for package in set(matched) if matched.count(package) > 1)
    if duplicates:
        fail(f"duplicate first-party dependencies in {project_file}: {', '.join(duplicates)}")
    if set(matched) != expected_packages:
        missing = sorted(expected_packages - set(matched))
        unexpected = sorted(set(matched) - expected_packages)
        details = []
        if missing:
            details.append(f"missing {', '.join(missing)}")
        if unexpected:
            details.append(f"unexpected {', '.join(unexpected)}")
        fail(f"first-party dependency mismatch in {project_file}: {'; '.join(details)}")

    project_file.write_text("".join(rewritten))


def validate_stage(
    stage_root: Path,
    expected_packages: set[str],
    local_dependency_url: str,
) -> None:
    project_text = (stage_root / "game.project").read_text()
    for package in expected_packages:
        expected_url = f"{local_dependency_url.rstrip('/')}/{package}.zip"
        if project_text.count(expected_url) != 1:
            fail(f"staged project does not contain exactly one {expected_url}")
        if f"github.com/ninecells-io/{package}/archive/" in project_text:
            fail(f"released {package} dependency remains in staged game.project")


def stage_consumer(
    consumer: Consumer,
    repo_root: Path,
    local_dependency_url: str,
) -> None:
    if shutil.which("rsync") is None:
        fail("rsync is required")
    if not (consumer.root / "game.project").is_file():
        fail(f"{consumer.identifier} project was not found at {consumer.root}")
    if consumer.stage_root in (consumer.root, repo_root):
        fail("staging root must not be a source repository")

    run_rsync(
        consumer.root,
        consumer.stage_root,
        "--delete",
        "--delete-excluded",
        "--exclude=/.git/",
        "--exclude=/.internal/",
        "--exclude=/build/",
        "--exclude=/.local-build/",
        "--exclude=/.env",
        "--exclude=/.DS_Store",
        "--exclude=/.externalToolBuilders/",
        "--exclude=/.project",
        "--exclude=/.cproject",
        "--exclude=/builtins/",
        "--exclude=/__pycache__/",
        "--exclude=*.pyc",
    )

    cached_libraries = consumer.root / ".internal" / "lib"
    if cached_libraries.is_dir():
        run_rsync(
            cached_libraries,
            consumer.stage_root / ".internal" / "lib",
            "--delete",
        )

    expected_packages = EXPECTED_PACKAGES[consumer.identifier]
    rewrite_dependencies(
        consumer.stage_root / "game.project",
        expected_packages,
        local_dependency_url,
    )
    validate_stage(consumer.stage_root, expected_packages, local_dependency_url)

    print(f"Staged {consumer.identifier} at {consumer.stage_root}")
    print("First-party dependencies point to the local dependency server.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "consumers",
        nargs="*",
        help="consumer identifiers; defaults to all supported consumers",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    configs = consumer_configs(repo_root)
    local_dependency_url = os.environ.get(
        "LOCAL_DEPENDENCY_URL", "http://127.0.0.1:9012"
    )
    for consumer in select_consumers(args.consumers, configs):
        stage_consumer(consumer, repo_root, local_dependency_url)
    return 0


if __name__ == "__main__":
    sys.exit(main())
