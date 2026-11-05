#!/bin/bash
# Copies EX Workbench into your plug-in folders and clears macOS's download quarantine so Ableton can load it.
cd "$(dirname "$0")" || exit 1
mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
rm -rf ~/Library/Audio/Plug-Ins/Components/"EX Workbench.component" ~/Library/Audio/Plug-Ins/VST3/"EX Workbench.vst3"
cp -R "EX Workbench.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "EX Workbench.vst3" ~/Library/Audio/Plug-Ins/VST3/
mkdir -p ~/Applications && rm -rf ~/Applications/"EX Workbench.app" && cp -R "EX Workbench.app" ~/Applications/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"EX Workbench.component" ~/Library/Audio/Plug-Ins/VST3/"EX Workbench.vst3" ~/Applications/"EX Workbench.app" 2>/dev/null
# make Logic/GarageBand (and macOS's AU cache) notice the new AU
killall -9 AudioComponentRegistrar 2>/dev/null
echo
echo "EX Workbench is installed:"
echo "  AU   -> ~/Library/Audio/Plug-Ins/Components"
echo "  VST3 -> ~/Library/Audio/Plug-Ins/VST3"
echo "  App  -> ~/Applications"
echo
echo "In Ableton: Settings > Plug-Ins > turn on 'Use Audio Units v2' and/or 'Use VST3 Plug-In System Folders', then Rescan."
read -n 1 -s -r -p "Press any key to close."
