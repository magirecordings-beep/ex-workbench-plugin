#!/bin/bash
# Copies Xtreme Lab into your plug-in folders and clears macOS's download quarantine so Ableton can load it.
cd "$(dirname "$0")" || exit 1
mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
rm -rf ~/Library/Audio/Plug-Ins/Components/"Xtreme Lab.component" ~/Library/Audio/Plug-Ins/VST3/"Xtreme Lab.vst3"
cp -R "Xtreme Lab.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "Xtreme Lab.vst3" ~/Library/Audio/Plug-Ins/VST3/
mkdir -p ~/Applications && rm -rf ~/Applications/"Xtreme Lab.app" && cp -R "Xtreme Lab.app" ~/Applications/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Xtreme Lab.component" ~/Library/Audio/Plug-Ins/VST3/"Xtreme Lab.vst3" ~/Applications/"Xtreme Lab.app" 2>/dev/null
# make Logic/GarageBand (and macOS's AU cache) notice the new AU
killall -9 AudioComponentRegistrar 2>/dev/null
echo
echo "Xtreme Lab is installed:"
echo "  AU   -> ~/Library/Audio/Plug-Ins/Components"
echo "  VST3 -> ~/Library/Audio/Plug-Ins/VST3"
echo "  App  -> ~/Applications"
echo
echo "In Ableton: Settings > Plug-Ins > turn on 'Use Audio Units v2' and/or 'Use VST3 Plug-In System Folders', then Rescan."
read -n 1 -s -r -p "Press any key to close."
