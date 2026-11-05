#!/usr/bin/env python3
"""Check the real engine, including disk reuse across separate processes."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('engine', type=Path, help='Path to the bundled demo executable')
args = parser.parse_args()
engine_path = args.engine.resolve()
project = engine_path.parent.parent / 'Resources/game.projectc'
if not project.is_file():
    project = engine_path.parent / 'game.projectc'
if not project.is_file():
    raise SystemExit('Pass a bundled demo executable with an adjacent game.projectc (or macOS Contents/Resources/game.projectc)')
engine = str(engine_path)
for configs, marker in [
    (['gallery.smoke=1'], 'GALLERY_SMOKE_PASSED'),
    (['gallery.page=7', 'gallery.cache=1', 'gallery.verify_cache=1'], 'GALLERY_RESTART_CACHE_PASSED'),
    (['gallery.smoke=1', 'display.window_width=960', 'display.window_height=600'], 'GALLERY_SMOKE_PASSED'),
]:
    result = subprocess.run([engine, '--config=profiler.port=17902'] + ['--config='+c for c in configs] + [str(project)],
                            cwd=project.parent,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
    if result.returncode or marker not in result.stdout or 'ERROR:SCRIPT:' in result.stdout or 'GALLERY_SMOKE_FAILED' in result.stdout:
        print(result.stdout)
        raise SystemExit(result.returncode or 1)
    print(marker)
