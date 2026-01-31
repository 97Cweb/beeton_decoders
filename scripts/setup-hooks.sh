#!/bin/sh
set -e
git config core.hooksPath .githooks
echo "✅ Configured git core.hooksPath to use .githooks"
