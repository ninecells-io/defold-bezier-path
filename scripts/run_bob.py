#!/usr/bin/env python3
"""Run pinned Bob with temporary, redacted dependency authentication."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
from urllib.parse import quote


TOKEN_PLACEHOLDER = "__GITHUB_TOKEN__"
DOTENV_KEY = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def dotenv_values(path: Path) -> dict[str, str]:
    """Parse simple dotenv assignments without executing shell code."""
    if not path.is_file():
        return {}

    values: dict[str, str] = {}
    for line_number, raw_line in enumerate(path.read_text().splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].lstrip()
        key, separator, raw_value = line.partition("=")
        key = key.strip()
        if not separator or not DOTENV_KEY.fullmatch(key):
            raise ValueError(f"invalid .env assignment on line {line_number}")

        raw_value = raw_value.strip()
        if raw_value.startswith(("'", '"')):
            try:
                parsed = shlex.split(raw_value, comments=True, posix=True)
            except ValueError as error:
                raise ValueError(f"invalid quoted .env value on line {line_number}: {error}") from error
            if len(parsed) != 1:
                raise ValueError(f"invalid .env value on line {line_number}")
            value = parsed[0]
        else:
            value = re.split(r"\s+#", raw_value, maxsplit=1)[0].strip()
        values[key] = value
    return values


def github_token(repo_root: Path) -> str | None:
    environment_token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if environment_token:
        return environment_token
    values = dotenv_values(repo_root / ".env")
    return values.get("GITHUB_TOKEN") or values.get("GH_TOKEN")


def render_placeholders(
    settings_text: str,
    token: str | None,
    source_name: str,
) -> tuple[str, list[str]]:
    if TOKEN_PLACEHOLDER not in settings_text:
        return settings_text, []
    if not token:
        raise ValueError(
            f"{source_name} contains {TOKEN_PLACEHOLDER}; set GITHUB_TOKEN "
            "in the bezier-path .env file or environment"
        )
    encoded_token = quote(token, safe="")
    return settings_text.replace(TOKEN_PLACEHOLDER, encoded_token), [token, encoded_token]


def dependency_settings(project_text: str, token: str | None) -> tuple[str | None, list[str]]:
    dependency = re.compile(r"^dependencies#\d+\s*=.*$", re.MULTILINE)
    dependency_lines = dependency.findall(project_text)
    placeholder_lines = [line for line in dependency_lines if TOKEN_PLACEHOLDER in line]
    if not placeholder_lines:
        return None, []
    rendered, secrets = render_placeholders(
        "[project]\n" + "\n".join(dependency_lines) + "\n",
        token,
        "game.project",
    )
    return rendered, secrets


def redact(text: str, secrets: list[str]) -> str:
    for secret in secrets:
        if secret:
            text = text.replace(secret, "<redacted>")
    return text


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--java", required=True)
    parser.add_argument("--bob", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", default="build/default")
    parser.add_argument("--platform", required=True)
    parser.add_argument("--variant", required=True)
    parser.add_argument("--defoldsdk", required=True)
    parser.add_argument("--build-server", required=True)
    parser.add_argument(
        "--settings",
        action="append",
        default=[],
        help="Additional Bob settings file; may be specified more than once",
    )
    parser.add_argument("--archive", action="store_true")
    parser.add_argument("commands", nargs="*", default=["resolve", "build"])
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    project_root = Path(args.root).resolve()
    project_file = project_root / "game.project"
    if not project_file.is_file():
        print(f"Bob error: game.project was not found at {project_file}", file=sys.stderr)
        return 1

    try:
        token = github_token(repo_root)
        settings_text, secrets = dependency_settings(project_file.read_text(), token)
        rendered_settings: list[tuple[str, str]] = []
        if settings_text is not None:
            rendered_settings.append(("credentials", settings_text))
        for index, raw_path in enumerate(args.settings):
            settings_source = Path(raw_path).resolve()
            if not settings_source.is_file():
                raise ValueError(f"settings file was not found: {settings_source}")
            rendered, settings_secrets = render_placeholders(
                settings_source.read_text(),
                token,
                str(settings_source),
            )
            rendered_settings.append((f"settings-{index}", rendered))
            secrets.extend(settings_secrets)
    except (OSError, ValueError) as error:
        print(f"Bob authentication error: {error}", file=sys.stderr)
        return 1

    settings_paths: list[Path] = []
    settings_directory: Path | None = None
    command = [
        args.java,
        "-jar",
        args.bob,
        "--root",
        str(project_root),
        "--output",
        args.output,
        "--platform",
        args.platform,
        "--variant",
        args.variant,
        "--defoldsdk",
        args.defoldsdk,
        "--build-server",
        args.build_server,
    ]
    if args.archive:
        command.append("--archive")

    process: subprocess.Popen[str] | None = None
    try:
        if rendered_settings:
            # Bob 1.13.0 expects settings paths to be absolute when it turns them
            # into build inputs, and mounts their parent directories. Give each
            # invocation a private one-file directory so Bob cannot recursively
            # walk macOS's volatile shared temporary directory.
            local_build_root = Path(
                os.environ.get("LOCAL_BUILD_ROOT", repo_root / ".local-build")
            ).resolve()
            local_build_root.mkdir(mode=0o700, parents=True, exist_ok=True)
            settings_directory = Path(
                tempfile.mkdtemp(prefix="bob-settings-", dir=local_build_root)
            )
            for prefix, rendered in rendered_settings:
                descriptor, temporary_name = tempfile.mkstemp(
                    prefix=f"{prefix}-",
                    suffix=".settings",
                    dir=settings_directory,
                )
                settings_path = Path(temporary_name)
                settings_paths.append(settings_path)
                with os.fdopen(descriptor, "w") as settings_file:
                    settings_file.write(rendered)
                command.extend(["--settings", str(settings_path)])

        command.extend(args.commands or ["resolve", "build"])
        process = subprocess.Popen(
            command,
            cwd=project_root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(redact(line, secrets))
            sys.stdout.flush()
        return process.wait()
    except KeyboardInterrupt:
        if process is not None:
            process.terminate()
        return 130
    finally:
        for settings_path in settings_paths:
            settings_path.unlink(missing_ok=True)
        if settings_directory is not None:
            try:
                settings_directory.rmdir()
            except FileNotFoundError:
                pass


if __name__ == "__main__":
    sys.exit(main())
