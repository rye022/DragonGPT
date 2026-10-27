param([string]$InstallDir = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$target = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\','/')
$marker = Join-Path $target '.installation.json'
if ([IO.Path]::GetFileName($target) -ne 'DragonGPT' -or !(Test-Path -LiteralPath $marker)) { throw 'Expected a managed DragonGPT installation directory.' }
$manifest = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
if ($manifest.product -ne 'DragonGPT' -or [IO.Path]::GetFullPath($manifest.root).TrimEnd('\','/') -ne $target) { throw 'Installation manifest does not match this directory.' }
if (Get-Process -Name DragonGPT -ErrorAction SilentlyContinue) { throw 'Exit DragonGPT first.' }
$resolvedFiles = @()
foreach ($relative in $manifest.files) {
    $path = [IO.Path]::GetFullPath((Join-Path $target $relative))
    if (!$path.StartsWith($target + '\',[StringComparison]::OrdinalIgnoreCase) -or $relative -match '^data[\\/]') { throw 'Unsafe manifest path.' }
    $resolvedFiles += $path
}
# Remove only explicitly installed files. User data is always retained.
foreach ($path in $resolvedFiles) { if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force } }
Remove-Item -LiteralPath $marker -Force
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) '霜翎 DragonGPT.lnk'
if (Test-Path -LiteralPath $shortcutPath) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    if ($shortcut.TargetPath -eq (Join-Path $target 'DragonGPT.exe')) { Remove-Item -LiteralPath $shortcutPath -Force }
}
$startupShortcut = Join-Path ([Environment]::GetFolderPath('Startup')) 'DragonGPT 霜翎.lnk'
if (Test-Path -LiteralPath $startupShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($startupShortcut)
    if ($shortcut.TargetPath -eq (Join-Path $target 'DragonGPT.exe')) { Remove-Item -LiteralPath $startupShortcut -Force }
}
Write-Output "Application files removed. User data was retained. Default: $env:LOCALAPPDATA/DragonGPT/UserData; legacy: $target/data"
