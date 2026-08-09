#!/usr/bin/env python3
"""Serve local Defold libraries as deterministic ZIP archives."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import os
from pathlib import Path
import re
import sys
from typing import Mapping
import zipfile


EXCLUDED_DIRECTORIES = {
    ".git",
    ".internal",
    ".local-build",
    "__pycache__",
    "build",
    "example",
    "examples",
    "test",
    "tests",
}
EXCLUDED_FILES = {".DS_Store", ".env", ".project", ".cproject"}


@dataclass(frozen=True)
class Package:
    identifier: str
    root: Path

    @property
    def endpoint(self) -> str:
        return f"/{self.identifier}.zip"


def package_configs(
    repo_root: Path,
    environment: Mapping[str, str] | None = None,
) -> dict[str, Package]:
    values = os.environ if environment is None else environment
    definitions = (
        ("defold-bezier-path", None, repo_root),
        (
            "defold-controls",
            "DEFOLD_CONTROLS_ROOT",
            repo_root.parent / "defold-controls",
        ),
        (
            "defold-sudoku-playboard",
            "DEFOLD_SUDOKU_PLAYBOARD_ROOT",
            repo_root.parent / "defold-sudoku-playboard",
        ),
        (
            "defold-helpers",
            "DEFOLD_HELPERS_ROOT",
            repo_root.parent / "defold-helpers",
        ),
    )
    return {
        identifier: Package(
            identifier,
            Path(values.get(key, str(default)) if key else default).resolve(),
        )
        for identifier, key, default in definitions
    }


def setting_value(project_text: str, section: str, key: str) -> str | None:
    current_section = ""
    for raw_line in project_text.splitlines():
        line = raw_line.strip()
        if line.startswith("[") and line.endswith("]"):
            current_section = line[1:-1]
            continue
        if current_section != section or not line or line.startswith("#"):
            continue
        name, separator, value = line.partition("=")
        if separator and name.strip() == key:
            return value.strip()
    return None


def include_directories(package: Package) -> tuple[str, ...]:
    project_file = package.root / "game.project"
    if not project_file.is_file():
        raise ValueError(f"{package.identifier}: missing {project_file}")
    value = setting_value(project_file.read_text(), "library", "include_dirs")
    directories = tuple(part for part in re.split(r"[,\s]+", value or "") if part)
    if not directories:
        raise ValueError(f"{package.identifier}: game.project has no library.include_dirs")
    for directory in directories:
        path = package.root / directory
        if Path(directory).is_absolute() or ".." in Path(directory).parts:
            raise ValueError(f"{package.identifier}: unsafe include directory {directory!r}")
        if not path.is_dir():
            raise ValueError(f"{package.identifier}: missing include directory {path}")
    return directories


def excluded(relative_path: Path) -> bool:
    return (
        any(part in EXCLUDED_DIRECTORIES for part in relative_path.parts)
        or relative_path.name in EXCLUDED_FILES
        or relative_path.suffix == ".pyc"
    )


def archive_entries(package: Package) -> dict[str, bytes]:
    directories = include_directories(package)
    manifest = (
        "[project]\n"
        f"title = {package.identifier}\n\n"
        "[library]\n"
        f"include_dirs = {','.join(directories)}\n"
    ).encode()
    entries = {"game.project": manifest}
    for directory in directories:
        source_root = package.root / directory
        for path in sorted(source_root.rglob("*")):
            if not path.is_file() or path.is_symlink():
                continue
            relative_path = path.relative_to(package.root)
            if excluded(relative_path):
                continue
            entries[relative_path.as_posix()] = path.read_bytes()
    return entries


def build_archive(package: Package) -> tuple[bytes, str]:
    entries = archive_entries(package)
    directories: set[str] = set()
    for name in entries:
        parent = Path(name).parent
        while parent != Path("."):
            directories.add(f"{parent.as_posix()}/")
            parent = parent.parent

    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(directories):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.external_attr = (0o40755 << 16) | 0x10
            archive.writestr(info, b"")
        for name, content in sorted(entries.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content)
    payload = output.getvalue()
    return payload, f'"{hashlib.sha256(payload).hexdigest()}"'


class DependencyRequestHandler(BaseHTTPRequestHandler):
    server_version = "DefoldLocalDependencies/1"

    @property
    def packages(self) -> dict[str, Package]:
        return self.server.packages  # type: ignore[attr-defined]

    def log_message(self, format_string: str, *args: object) -> None:
        sys.stderr.write(
            f"{self.address_string()} - [{self.log_date_time_string()}] "
            f"{format_string % args}\n"
        )

    def send_payload(
        self,
        status: HTTPStatus,
        payload: bytes,
        content_type: str,
        *,
        etag: str | None = None,
        include_body: bool = True,
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-cache")
        if etag is not None:
            self.send_header("ETag", etag)
        self.end_headers()
        if include_body:
            self.wfile.write(payload)

    def handle_request(self, *, include_body: bool) -> None:
        path = self.path.partition("?")[0]
        if path in ("/", "/health"):
            self.send_payload(
                HTTPStatus.OK,
                b"local Defold dependencies ready\n",
                "text/plain; charset=utf-8",
                include_body=include_body,
            )
            return

        package = next(
            (candidate for candidate in self.packages.values() if candidate.endpoint == path),
            None,
        )
        if package is None:
            self.send_payload(
                HTTPStatus.NOT_FOUND,
                b"not found\n",
                "text/plain; charset=utf-8",
                include_body=include_body,
            )
            return

        try:
            payload, etag = build_archive(package)
        except (OSError, ValueError) as error:
            message = f"package error: {error}\n".encode()
            self.send_payload(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                message,
                "text/plain; charset=utf-8",
                include_body=include_body,
            )
            return

        if self.headers.get("If-None-Match") == etag:
            self.send_response(HTTPStatus.NOT_MODIFIED)
            self.send_header("ETag", etag)
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            return
        self.send_payload(
            HTTPStatus.OK,
            payload,
            "application/zip",
            etag=etag,
            include_body=include_body,
        )

    def do_GET(self) -> None:
        self.handle_request(include_body=True)

    def do_HEAD(self) -> None:
        self.handle_request(include_body=False)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9012)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    packages = package_configs(repo_root)
    try:
        for package in packages.values():
            include_directories(package)
    except ValueError as error:
        print(f"dependency server error: {error}", file=sys.stderr)
        return 1

    server = ThreadingHTTPServer((args.host, args.port), DependencyRequestHandler)
    server.packages = packages  # type: ignore[attr-defined]
    print(f"Serving local Defold dependencies at http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
