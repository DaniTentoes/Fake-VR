# Automated Release Publisher for Fake-VR
param (
    [string]$Version = "1.0.1",
    [string]$Changelog = "Maintenance release with updates and fixes"
)

$ErrorActionPreference = "Stop"
Write-Host "=========================================="
Write-Host "   PUBLISHING FAKE-VR RELEASE v$Version"
Write-Host "=========================================="

# 1. Update version.json
$manifest = @{
    version = $Version
    download_url = "https://github.com/DaniTentoes/Fake-VR/releases/download/v$Version/FakeVR_Friend_Client.zip"
    changelog = $Changelog
} | ConvertTo-Json -Depth 3
$manifest | Set-Content "version.json" -Encoding ASCII

# 2. Package client release zip
Write-Host "Packaging FakeVR_Friend_Client.zip..."
$staging = "FakeVR_Client_Release"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Path "$staging\bin" -Force | Out-Null
New-Item -ItemType Directory -Path "$staging\assets" -Force | Out-Null

Copy-Item "bin\FakeVR_Client.exe" "$staging\bin\" -Force
Copy-Item "bin\FakeVR_Launcher.exe" "$staging\bin\" -Force
Copy-Item "bin\phonon.dll" "$staging\bin\" -Force
Copy-Item "bin\nvdaControllerClient64.dll" "$staging\bin\" -Force
Copy-Item "bin\nvdaControllerClient32.dll" "$staging\bin\" -Force
Copy-Item "bin\GPUUtilities.dll" "$staging\bin\" -Force
Copy-Item "bin\TrueAudioNext.dll" "$staging\bin\" -Force
Copy-Item "assets\*" "$staging\assets\" -Recurse -Force
Copy-Item "version.json" "$staging\" -Force
Copy-Item "Play_FakeVR.bat" "$staging\" -Force

$readmeText = @"
Fake-VR (Audio-First Social Cyberspace)
=======================================

HOW TO PLAY:
1. Make sure your headphones are plugged in and stereo sound is enabled.
2. If you use NVDA, make sure it is running.
3. Double-click Play_FakeVR.bat (it will automatically check for updates).
4. Simply press Enter at the prompts to accept the defaults:
   - Host: bore.pub
   - Port: 32827
   - Username: (Press Enter for Player, or type your name)

CONTROLS:
- W / A / S / D : Move Forward / Left / Backward / Right
- Left / Right Arrow : Turn 30 degrees left / right
- Spacebar : Strike / attack objects directly in front of you
- U : Echolocation snap pulse (8-ray omnidirectional room scan)
- T : Change outfit (Regular Clothes, Naked, Suit of Armor)
- P : Check other players nearby and their distance
- C : Check your coordinates, room region, and clothing
- Escape / Q : Quit to desktop
"@
$readmeText | Set-Content "$staging\README.txt" -Encoding ASCII

$zipName = "FakeVR_Friend_Client.zip"
if (Test-Path $zipName) { Remove-Item -Force $zipName }
Compress-Archive -Path "$staging\*" -DestinationPath $zipName -Force
Remove-Item -Recurse -Force $staging

# 3. Commit and push version.json to git
Write-Host "Updating repository and git tag..."
git add version.json
git commit -m "Release v$Version: $Changelog"
git push origin main

# 4. Upload release to GitHub via gh CLI
Write-Host "Uploading release to GitHub..."
gh release create "v$Version" $zipName --title "Fake-VR v$Version" --notes "$Changelog"

Write-Host "SUCCESS! Release v$Version published. All connected clients will auto-update on launch!"
