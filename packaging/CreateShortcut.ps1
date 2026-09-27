$exePath = "C:\mahali\mahali-desktop.exe"
$iconPath = "C:\mahali\mahali-green.ico"
$desktop = [Environment]::GetFolderPath("Desktop")
$shortcutPath = Join-Path $desktop "Mahali.lnk"

$WshShell = New-Object -ComObject WScript.Shell
$Shortcut = $WshShell.CreateShortcut($shortcutPath)
$Shortcut.TargetPath = $exePath
$Shortcut.IconLocation = "$iconPath,0"
$Shortcut.WorkingDirectory = "C:\mahali"
$Shortcut.Description = "Mahali - Point de vente"
$Shortcut.Save()

Write-Host "Raccourci créé sur le bureau."
