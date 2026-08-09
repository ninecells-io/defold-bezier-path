SHELL := /bin/bash

PYTHON ?= python3
DEFOLD_CONTROLS_ROOT ?= $(abspath ../defold-controls)
DEFOLD_SUDOKU_PLAYBOARD_ROOT ?= $(abspath ../defold-sudoku-playboard)
ONE_SUDOKU_ROOT ?= $(abspath ../one-sudoku)
CONSUMERS ?= defold-controls defold-sudoku-playboard one-sudoku
PRODUCTION_PLATFORM ?= arm64-macos
PRODUCTION_VARIANT ?= release

export DEFOLD_CONTROLS_ROOT
export DEFOLD_SUDOKU_PLAYBOARD_ROOT
export ONE_SUDOKU_ROOT
export CONSUMERS
export PRODUCTION_PLATFORM
export PRODUCTION_VARIANT

.PHONY: setup-bob setup-local-extender setup-check extender-start extender-stop extender-status \
	test stage-consumers stage-app dev-build watch production-build clean-local-build

setup-bob:
	./scripts/ensure_bob.sh

setup-local-extender:
	./scripts/setup_local_extender.sh

setup-check:
	./scripts/setup_local_extender.sh --check-only

extender-start:
	./scripts/extender.sh start

extender-stop:
	./scripts/extender.sh stop

extender-status:
	./scripts/extender.sh status

test:
	$(MAKE) -C benchmark test
	PYTHONPYCACHEPREFIX=/private/tmp/bezierpath-pycache $(PYTHON) -m unittest discover -s scripts/tests -v

stage-consumers:
	$(PYTHON) scripts/stage_consumers.py $(CONSUMERS)

stage-app:
	$(PYTHON) scripts/stage_consumers.py one-sudoku

dev-build: test
	./scripts/dev_build.sh

watch:
	./scripts/extender.sh ensure
	$(PYTHON) scripts/watch.py

production-build: test setup-bob
	./scripts/production_build.sh

clean-local-build:
	@echo "Refusing to remove .local-build automatically because it contains the packaged Xcode SDK."
	@echo "Remove $(CURDIR)/.local-build manually if you intend to reprovision it."
