#!/usr/bin/env python3
"""Watch bezier-path native sources and serialize development builds."""

from __future__ import annotations

from datetime import datetime
import hashlib
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def timestamp() -> str:
    return datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %Z")


def log(message: str) -> None:
    print(f"[{timestamp()}] {message}", flush=True)


def watched_files(repo_root: Path) -> list[Path]:
    files = list((repo_root / "bezierpath" / "src").rglob("*.cpp"))
    files.extend((repo_root / "bezierpath" / "include").rglob("*.h"))
    files.append(repo_root / "bezierpath" / "ext.manifest")
    return sorted(path for path in files if path.is_file())


def source_digest(repo_root: Path) -> str:
    digest = hashlib.sha256()
    for path in watched_files(repo_root):
        digest.update(str(path.relative_to(repo_root)).encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def terminate(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def main() -> int:
    script_repo_root = Path(__file__).resolve().parent.parent
    repo_root = Path(os.environ.get("WATCH_ROOT", script_repo_root)).resolve()
    build_cwd = Path(os.environ.get("WATCH_BUILD_CWD", script_repo_root)).resolve()
    poll_seconds = float(os.environ.get("WATCH_POLL_SECONDS", "0.25"))
    debounce_seconds = float(os.environ.get("WATCH_DEBOUNCE_SECONDS", "0.6"))
    build_command = os.environ.get("WATCH_BUILD_COMMAND")
    command = ["/usr/bin/make", "--no-print-directory", "dev-build"] if not build_command else ["/bin/bash", "-lc", build_command]

    if not watched_files(repo_root):
        log("No native source files found; refusing to watch.")
        return 1

    current_digest = source_digest(repo_root)
    dirty_since: float | None = None
    pending = True
    process: subprocess.Popen[bytes] | None = None
    build_started = 0.0

    log(f"Watching {len(watched_files(repo_root))} native files. Press Ctrl-C to stop.")
    try:
        while True:
            now = time.monotonic()
            new_digest = source_digest(repo_root)
            if new_digest != current_digest:
                current_digest = new_digest
                dirty_since = now
                log("Native source change detected; waiting for saves to settle.")

            if dirty_since is not None and now - dirty_since >= debounce_seconds:
                pending = True
                dirty_since = None
                if process is not None:
                    log("Build is running; queued one follow-up build.")

            if process is not None:
                result = process.poll()
                if result is not None:
                    elapsed = time.monotonic() - build_started
                    state = "succeeded" if result == 0 else f"failed with exit code {result}"
                    log(f"Build {state} after {elapsed:.1f}s.")
                    process = None

            if process is None and pending and dirty_since is None:
                pending = False
                build_started = time.monotonic()
                log("Starting development build.")
                process = subprocess.Popen(command, cwd=build_cwd, start_new_session=True)

            time.sleep(poll_seconds)
    except KeyboardInterrupt:
        log("Stopping watcher.")
        if process is not None:
            terminate(process)
        return 0


if __name__ == "__main__":
    sys.exit(main())
