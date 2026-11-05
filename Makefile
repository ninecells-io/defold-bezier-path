SHELL := /bin/bash
PYTHON ?= python3
LUA ?= luajit
PLATFORM ?= arm64-macos
BUILD_SERVER ?= https://build.defold.com
export PLATFORM BUILD_SERVER

.PHONY: test build bundle setup-bob setup-check setup-local-extender extender-start extender-stop extender-status

test:
	$(MAKE) -C benchmark test
	$(MAKE) -C benchmark lua-test LUA="$(LUA)"
	$(LUA) tests/bezierpath_texture_api_test.lua
	$(LUA) tests/texture_cache_test.lua
	PYTHONPYCACHEPREFIX=/tmp/bezierpath-pycache $(PYTHON) -m unittest discover -s scripts/tests -v

setup-bob:
	./scripts/ensure_bob.sh
build:
	./scripts/build_demo.sh
bundle:
	./scripts/build_demo.sh bundle
setup-check:
	./scripts/setup_local_extender.sh --check-only
setup-local-extender:
	./scripts/setup_local_extender.sh
extender-start:
	./scripts/extender.sh start
extender-stop:
	./scripts/extender.sh stop
extender-status:
	./scripts/extender.sh status
