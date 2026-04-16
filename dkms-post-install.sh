#!/bin/bash
set -e

SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SOURCE_DIR"

make install-extra
