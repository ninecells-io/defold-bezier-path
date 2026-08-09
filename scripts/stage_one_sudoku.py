#!/usr/bin/env python3
"""Create an isolated one-sudoku build root using this local extension."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import NoReturn


RELEASE_DEPENDENCY_MARKER = "defold-bezier-path/archive/"


def fail(message: str) -> NoReturn:
    raise SystemExit(f"stage error: {message}")


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
        rewritten.append(f"{match.group(1)}dependencies#{next_index}{match.group(2)}{value}{newline}")
        next_index += 1

    if found == 0:
        fail(f"no project dependencies found in {project_file}")
    if removed != 1:
        fail(f"expected exactly one released bezier-path dependency, removed {removed}")

    project_file.write_text("".join(rewritten))


def validate_stage(stage_root: Path, local_extension: Path) -> None:
    project_text = (stage_root / "game.project").read_text()
    if RELEASE_DEPENDENCY_MARKER in project_text:
        fail("released bezier-path dependency is still present in staged game.project")

    staged_manifest = stage_root / "bezierpath" / "ext.manifest"
    if not staged_manifest.is_file():
        fail("local extension manifest is missing from the staging root")
    if staged_manifest.read_bytes() != (local_extension / "ext.manifest").read_bytes():
        fail("staged extension does not match the local extension")


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    local_build_root = Path(os.environ.get("LOCAL_BUILD_ROOT", repo_root / ".local-build")).resolve()
    app_root_value = os.environ.get("ONE_SUDOKU_ROOT")
    app_root = Path(app_root_value).resolve() if app_root_value else (repo_root.parent / "one-sudoku").resolve()
    stage_root = Path(os.environ.get("APP_STAGE_ROOT", local_build_root / "one-sudoku-stage")).resolve()
    local_extension = repo_root / "bezierpath"

    if shutil.which("rsync") is None:
        fail("rsync is required")
    if not (app_root / "game.project").is_file():
        fail(f"one-sudoku project was not found at {app_root}")
    if not (local_extension / "ext.manifest").is_file():
        fail(f"local extension was not found at {local_extension}")
    if stage_root == app_root or stage_root == repo_root:
        fail("staging root must not be a source repository")

    run_rsync(
        app_root,
        stage_root,
        "--delete",
        "--exclude=/.git/",
        "--exclude=/.internal/",
        "--exclude=/build/",
        "--exclude=/.DS_Store",
    )

    cached_libraries = app_root / ".internal" / "lib"
    if cached_libraries.is_dir():
        run_rsync(cached_libraries, stage_root / ".internal" / "lib")

    rewrite_dependencies(stage_root / "game.project")
    run_rsync(local_extension, stage_root / "bezierpath", "--delete", "--exclude=.DS_Store")
    validate_stage(stage_root, local_extension)

    print(f"Staged one-sudoku at {stage_root}")
    print("Released bezier-path dependency removed; local bezierpath/ injected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
