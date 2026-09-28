#!/usr/bin/env bash
set -e

echo "=== Installing Stack CLI ==="

# 1. Check for xmake
if ! command -v xmake &>/dev/null; then
  echo "Error: xmake is required but not installed."
  exit 1
fi

# 2. Clone repository into a temporary folder
TMP_DIR=$(mktemp -d)
echo "Downloading source files..."
git clone --quiet https://github.com/JordanGrant3D/stack.git "$TMP_DIR/stack"
cd "$TMP_DIR/stack"

# 3. Configure, build, and install directly using xmake
echo "Building project in release mode..."
xmake f -m release
xmake build

INSTALL_DIR="$HOME/.local/bin"
echo "Installing binary to $INSTALL_DIR..."
xmake install -o "$INSTALL_DIR"

# 4. Add ~/.local/bin to PATH in ~/.bashrc if needed
RC_FILE="$HOME/.bashrc"
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'

if [ -f "$RC_FILE" ] && ! grep -qF "$PATH_LINE" "$RC_FILE"; then
  echo -e "\n# Added by Stack installer\n$PATH_LINE" >>"$RC_FILE"
fi

# 5. Cleanup
rm -rf "$TMP_DIR"

echo ""
echo "=================================================="
echo "Installation completed successfully!"
echo "Run 'source ~/.bashrc' to update your terminal."
echo "After that, you can use 'stack' from anywhere!"
echo "=================================================="
