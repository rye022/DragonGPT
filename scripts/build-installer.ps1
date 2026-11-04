param(
    [Parameter(Mandatory=$true)][string]$Iscc,
    [string]$OutputDir=(Join-Path (Split-Path -Parent $PSScriptRoot) 'outputs'),
    [string]$RuntimeDir=(Join-Path (Split-Path -Parent $PSScriptRoot) 'dist/DragonGPT')
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtime=[IO.Path]::GetFullPath($RuntimeDir)
if(!(Test-Path -LiteralPath "$runtime/DragonGPT.exe")) { throw 'Build and package the tested runtime first.' }
$versionMatch=[regex]::Match((Get-Content -LiteralPath "$project/CMakeLists.txt" -Raw),'project\(DragonGPT VERSION ([0-9.]+)')
if(!$versionMatch.Success) { throw 'Cannot determine the application version.' }
$version=$versionMatch.Groups[1].Value
$out=[IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $Iscc "/DSourceRoot=$project" "/DRuntimeRoot=$runtime" "/DOutputRoot=$out" "$project/installer/DragonGPT.iss"
if($LASTEXITCODE) { throw 'Installer compilation failed' }
Get-FileHash -LiteralPath "$out/DragonGPT-Setup-v$version-win-x64.exe" -Algorithm SHA256
