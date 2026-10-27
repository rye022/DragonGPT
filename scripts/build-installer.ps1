param([Parameter(Mandatory=$true)][string]$Iscc,[string]$OutputDir=(Split-Path -Parent $PSScriptRoot | Split-Path -Parent))
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!(Test-Path -LiteralPath "$project/dist/DragonGPT/DragonGPT.exe")) { throw 'Build and package the tested runtime first.' }
$out=[IO.Path]::GetFullPath($OutputDir)
& $Iscc "/DSourceRoot=$project" "/DOutputRoot=$out" "$project/installer/DragonGPT.iss"
if($LASTEXITCODE) { throw 'Installer compilation failed' }
Get-FileHash -LiteralPath "$out/DragonGPT-Setup-v3.0.0-win-x64.exe" -Algorithm SHA256
