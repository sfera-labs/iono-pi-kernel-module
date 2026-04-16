#!/bin/bash
set -e

SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SOURCE_DIR"

CURRENT_VERSION="$(tr -d '[:space:]' < VERSION 2>/dev/null || true)"

# Keep shared artifacts when another ionopi DKMS version is still present.
if command -v dkms >/dev/null 2>&1 && [ -n "$CURRENT_VERSION" ]; then
	OTHER_VERSIONS_COUNT="$(dkms status 2>/dev/null | grep -Ec '^ionopi/' || true)"
	THIS_VERSION_COUNT="$(dkms status 2>/dev/null | grep -Ec "^ionopi/${CURRENT_VERSION}," || true)"

	if [ "$OTHER_VERSIONS_COUNT" -gt "$THIS_VERSION_COUNT" ]; then
		echo "Skipping uninstall-extra: another ionopi DKMS version is present"
		exit 0
	fi
fi

make uninstall-extra
