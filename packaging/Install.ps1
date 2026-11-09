# Copies Xtreme Lab.vst3 into the standard VST3 folder and the app to the Desktop.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$vst3 = Join-Path $env:CommonProgramFiles "VST3"
New-Item -ItemType Directory -Force $vst3 | Out-Null
Remove-Item -Recurse -Force (Join-Path $vst3 "Xtreme Lab.vst3") -ErrorAction SilentlyContinue
Copy-Item -Recurse (Join-Path $here "Xtreme Lab.vst3") $vst3
Get-ChildItem -Recurse (Join-Path $vst3 "Xtreme Lab.vst3") | Unblock-File
$app = Join-Path ([Environment]::GetFolderPath("Desktop")) "Xtreme Lab"
New-Item -ItemType Directory -Force $app | Out-Null
Copy-Item (Join-Path $here "Xtreme Lab.exe") $app -Force
Unblock-File (Join-Path $app "Xtreme Lab.exe")
Write-Host ""
Write-Host "Xtreme Lab is installed:"
Write-Host "  VST3 -> $vst3\Xtreme Lab.vst3"
Write-Host "  App  -> $app\Xtreme Lab.exe"
Write-Host ""
Write-Host "In Ableton: Options > Preferences > Plug-Ins > Use VST3 Plug-in System Folder: On, then Rescan."
Read-Host "Press Enter to close"
