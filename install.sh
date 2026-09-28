#!/usr/bin/env bash
set -e

echo "=== Installing Stack CLI ==="

# 1. Check for xmake and git
if ! command -v xmake &>/dev/null; then
  echo "Error: xmake is required but not installed. Please install xmake first."
  exit 1
fi

# 2. Clone the repository into a temporary folder
TMP_DIR=$(mktemp -d)
echo "Downloading source files..."
git clone --quiet https://github.com/JordanGrant3D/stack.git "$TMP_DIR/stack"
cd "$TMP_DIR/stack"

# 3. Build using xmake
echo "Building project in release mode..."
xmake f -m release --quiet
xmake build --quiet

# 4. Install binary to ~/.local/bin
INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"
cp "$(xmake l find-targetfile)" "$INSTALL_DIR/stack"
chmod +x "$INSTALL_DIR/stack"

# 5. Configure ~/.bashrc PATH if needed
RC_FILE="$HOME/.bashrc"
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'

if [ -f "$RC_FILE" ]; then
  if ! grep -qF "$PATH_LINE" "$RC_FILE"; then
    echo "" >>"$RC_FILE"
    echo "# Added by Stack installer" >>"$RC_FILE"
    echo "$PATH_LINE" >>"$RC_FILE"
  fi
fi

# 6. Cleanup temporary files
rm -rf "$TMP_DIR"

echo ""
echo "=================================================="
echo "Installation complete successfully!"
echo "Run this command to update your current terminal:"
echo "    source ~/.bashrc"
echo "After that, you can use 'stack' from anywhere!"
echo "=================================================="
