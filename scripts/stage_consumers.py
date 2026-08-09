#!/usr/bin/env python3
"""Create isolated consumer build roots using this local extension."""

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


RELEASE_DEPENDENCY_MARKER = "defold-bezier-path/archive/"
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


def rewrite_dependencies(project_file: Path) -> None:
    lines = project_file.read_text().splitlines(keepends=True)
    dependency = re.compile(r"^(\s*)dependencies#\d+(\s*=\s*)(.*?)(\r?\n)?$")
    in_project = False
    found = 0
    removed = 0
    next_index = 0
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
        value = match.group(3)
        if RELEASE_DEPENDENCY_MARKER in value:
            removed += 1
            continue

        newline = match.group(4) or ""
        rewritten.append(
            f"{match.group(1)}dependencies#{next_index}{match.group(2)}{value}{newline}"
        )
        next_index += 1

    if found == 0:
        fail(f"no project dependencies found in {project_file}")
    if removed != 1:
        fail(f"expected exactly one released bezier-path dependency, removed {removed}")

    project_file.write_text("".join(rewritten))


def extension_contents(extension_root: Path) -> dict[Path, bytes]:
    return {
        path.relative_to(extension_root): path.read_bytes()
        for path in sorted(extension_root.rglob("*"))
        if path.is_file() and path.name != ".DS_Store"
    }


def validate_stage(stage_root: Path, local_extension: Path) -> None:
    project_text = (stage_root / "game.project").read_text()
    if RELEASE_DEPENDENCY_MARKER in project_text:
        fail("released bezier-path dependency is still present in staged game.project")

    staged_extension = stage_root / "bezierpath"
    required_files = (
        staged_extension / "ext.manifest",
        staged_extension / "bezierpath.lua",
        staged_extension / "src",
        staged_extension / "include",
    )
    if not all(path.exists() for path in required_files):
        fail("local Lua or native extension sources are missing from the staging root")
    if extension_contents(staged_extension) != extension_contents(local_extension):
        fail("staged extension does not match the local extension")


def stage_consumer(
    consumer: Consumer,
    repo_root: Path,
    local_extension: Path,
) -> None:
    if shutil.which("rsync") is None:
        fail("rsync is required")
    if not (consumer.root / "game.project").is_file():
        fail(f"{consumer.identifier} project was not found at {consumer.root}")
    if not (local_extension / "ext.manifest").is_file():
        fail(f"local extension was not found at {local_extension}")
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

    rewrite_dependencies(consumer.stage_root / "game.project")
    run_rsync(
        local_extension,
        consumer.stage_root / "bezierpath",
        "--delete",
        "--exclude=.DS_Store",
    )
    validate_stage(consumer.stage_root, local_extension)

    print(f"Staged {consumer.identifier} at {consumer.stage_root}")
    print("Released bezier-path dependency removed; local bezierpath/ injected.")


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
    local_extension = repo_root / "bezierpath"
    configs = consumer_configs(repo_root)
    for consumer in select_consumers(args.consumers, configs):
        stage_consumer(consumer, repo_root, local_extension)
    return 0


if __name__ == "__main__":
    sys.exit(main())
