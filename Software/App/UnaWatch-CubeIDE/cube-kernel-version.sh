#!/bin/bash

set -e

# Source version script to get BUILD_VERSION
. "$(dirname "$0")/../../../SDK/Utilities/Scripts/build-cube/una-version.sh"


# Patch __BUILD_VERSION__ in the imported project's prefs (assumes project name matches basename of IMPORT_PATH)
PREFS_FILE="$(dirname "$0")/.settings/org.eclipse.cdt.core.prefs"
if [ ! -f "$PREFS_FILE" ]; then
  echo "Warning: Prefs file not found at $PREFS_FILE"
fi

if [ -n "$BUILD_VERSION" ]; then
sed -i 's#/__BUILD_VERSION__/value=.*#/__BUILD_VERSION__/value='"$BUILD_VERSION"'#g' "$PREFS_FILE" \
    && echo "Overridden __BUILD_VERSION__ to '$BUILD_VERSION' in $PREFS_FILE" \
    || echo "Pattern __BUILD_VERSION__ not found in $PREFS_FILE"
fi