#!/bin/sh
# One-time setup: point git at the versioned hooks directory so the
# style gates (.githooks/pre-commit, .githooks/pre-push) run on every
# clone that enables them.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname "$here")
cd "$root"

git config core.hooksPath .githooks
chmod +x .githooks/pre-commit .githooks/pre-push
echo "git hooks installed: core.hooksPath -> .githooks"
echo "  pre-commit: clang-format + condition parens (fast)"
echo "  pre-push:   tools/check-style.sh (full gate)"
