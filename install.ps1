param(
    [string]$InstallDir = "$env:LOCALAPPDATA/Programs/DragonGPT",
    [switch]$DesktopShortcut,
    [switch]$AutoStart,
    [switch]$Start
)
$ErrorActionPreference = 'Stop'
$source = if (Test-Path -LiteralPath "$PSScriptRoot/DragonGPT.exe") { $PSScriptRoot } else { "$PSScriptRoot/dist/DragonGPT" }
if (!(Test-Path -LiteralPath "$source/DragonGPT.exe")) { throw 'Extract the complete release package first.' }
$target = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\','/')
if ([IO.Path]::GetFileName($target) -ne 'DragonGPT') { throw 'Installation directory must end with DragonGPT.' }
$marker = Join-Path $target '.installation.json'
if ((Test-Path -LiteralPath $target) -and !(Test-Path -LiteralPath $marker)) { throw 'Destination already exists without a DragonGPT installation manifest. Choose another parent directory.' }
if (Get-Process -Name DragonGPT -ErrorAction SilentlyContinue) { throw 'Exit DragonGPT before installing or updating.' }
New-Item -ItemType Directory -Force $target | Out-Null
if (Test-Path -LiteralPath (Join-Path $target 'DragonGPT.exe')) {
    $backupParent = Join-Path ([IO.Path]::GetDirectoryName($target)) 'DragonGPT-upgrade-backups'
    $backup = Join-Path $backupParent (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $target 'DragonGPT.exe') -Destination $backup
    if (Test-Path -LiteralPath (Join-Path $target 'data')) {
        Copy-Item -LiteralPath (Join-Path $target 'data') -Destination $backup -Recurse
    }
    Write-Output "Rollback backup: $backup"
}
$sourceFull = [IO.Path]::GetFullPath($source).TrimEnd('\','/')
$files = @()
Get-ChildItem -LiteralPath $sourceFull -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($sourceFull.Length + 1)
    if ($relative -notmatch '^data[\\/]' -and $relative -notmatch '(^|[\\/])(runtime|model|models|__pycache__)([\\/]|$)' -and $relative -notmatch '(^|[\\/])ready\.json$' -and $relative -notmatch '\.(dpapi|sqlite[^\\/]*|ini|log)$' -and $relative -ne '.installation.json') {
        $destination = [IO.Path]::GetFullPath((Join-Path $target $relative))
        if (!$destination.StartsWith($target + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected installation path.' }
        New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
        $files += $relative
    }
}
@{product='DragonGPT';version='4.0.0';root=$target;files=$files} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $marker -Encoding utf8
if ($DesktopShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) '霜翎 DragonGPT.lnk'))
    $shortcut.TargetPath = Join-Path $target 'DragonGPT.exe'
    $shortcut.Arguments = '--desktop'
    $shortcut.WindowStyle = 1
    $shortcut.IconLocation = (Join-Path $target 'DragonGPT.exe') + ",0"
    $shortcut.WorkingDirectory = $target
    $shortcut.Save()
}
if ($AutoStart) {
    $startup = [Environment]::GetFolderPath('Startup')
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path $startup 'DragonGPT 霜翎.lnk'))
    $shortcut.TargetPath = Join-Path $target 'DragonGPT.exe'
    $shortcut.Arguments = '--desktop'
    $shortcut.WorkingDirectory = $target
    $shortcut.WindowStyle = 1
    $shortcut.IconLocation = (Join-Path $target 'DragonGPT.exe') + ',0'
    $shortcut.Description = '登录 Windows 后在桌面显示霜翎'
    $shortcut.Save()
}
if ($Start) { Start-Process -FilePath (Join-Path $target 'DragonGPT.exe') -ArgumentList '--desktop' -WindowStyle Hidden }
Write-Output "Installed: $target"
