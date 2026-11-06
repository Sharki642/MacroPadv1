#!/bin/sh
# Adds MacroPad Manager with its icon to the KDE application menu.
DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET="$HOME/.local/share/applications/macropad-manager.desktop"
mkdir -p "$(dirname "$TARGET")"
cat > "$TARGET" <<DESKTOP
[Desktop Entry]
Type=Application
Name=MacroPad Manager
Comment=Change what the macropad keys do
Exec=python3 "$DIR/macropad_manager.py"
Icon=$DIR/macropad.png
Terminal=false
Categories=Utility;
StartupWMClass=macropad-manager
DESKTOP
update-desktop-database "$(dirname "$TARGET")" 2>/dev/null
echo "Installed $TARGET"
