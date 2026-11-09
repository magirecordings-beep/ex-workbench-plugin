Xtreme Lab (plugin) - plays E-MU Emulator X banks (.exb + SamplePool) in Ableton, FL Studio, Cubase and other hosts.

INSTALL
1. Unzip this folder.
2. Double-click "Install.bat". Windows asks for permission (the VST3 folder is a system folder). It copies
   "Xtreme Lab.vst3" to C:\Program Files\Common Files\VST3 and the standalone app to your Desktop folder
   "Xtreme Lab". If Windows SmartScreen says "Windows protected your PC", click More info > Run anyway
   (the plugin isn't code-signed).
   Or copy "Xtreme Lab.vst3" into C:\Program Files\Common Files\VST3 yourself.
3. In Ableton: Options > Preferences > Plug-Ins > turn on "Use VST3 Plug-in System Folder", then Rescan.

The faceplate uses Microsoft Edge WebView2, which is part of Windows 10 and 11. If the window stays blank,
install the "WebView2 Runtime" from Microsoft's website.

USE
- Click "Load bank..." and choose the .exb. Its SamplePool is found automatically: a "SamplePool" or
  "<bank name> SamplePool" folder next to the .exb, or .ebl files lying beside it.
- Your project saves the bank location, preset, knobs and settings.
- Program changes pick presets. Knobs A-P listen to X3's default CCs (21-28, 70-73, 91, 93, 82, 83);
  click a knob's tag to MIDI-learn it. Everything can be automated.
