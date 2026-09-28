#!/usr/bin/env bash
set -e

echo "=== Uninstalling Stack CLI ==="

# 1. Remove the binary from ~/.local/bin
TARGET_BIN="$HOME/.local/bin/stack"
if [ -f "$TARGET_BIN" ]; then
  rm -f "$TARGET_BIN"
  echo "Removed stack binary from $TARGET_BIN"
else
  echo "Stack binary not found in $TARGET_BIN"
fi

# 2. Clean up the PATH addition from ~/.bashrc (optional)
RC_FILE="$HOME/.bashrc"
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'

if [ -f "$RC_FILE" ]; then
  # Create a temporary file without the path line and comment
  grep -v '# Added by Stack installer' "$RC_FILE" | grep -v 'export PATH="\$HOME/.local/bin:\$PATH"' >"$RC_FILE.tmp" && mv "$RC_FILE.tmp" "$RC_FILE"
  echo "Cleaned up PATH configuration from $RC_FILE (if it existed)"
fi

echo ""
echo "=================================================="
echo "Uninstallation complete!"
echo "You may need to restart your terminal or run:"
echo "    source ~/.bashrc"
echo "=================================================="
