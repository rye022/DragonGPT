param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$CompilerDir,
    [string]$BuildDir="$PSScriptRoot/build/bond",
    [string]$ReviewDir="$PSScriptRoot/build/bond-review",
    [switch]$LiveBlink
)
$ErrorActionPreference='Stop'
$env:PATH="$QtRoot/bin;$CompilerDir;$env:PATH"
$env:QT_QPA_PLATFORM='windows'
cmake -S $PSScriptRoot -B $BuildDir -G Ninja "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$CompilerDir/g++.exe" -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE){throw 'Configure failed'}
cmake --build $BuildDir --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
ctest --test-dir $BuildDir --output-on-failure
if($LASTEXITCODE){throw 'Regression failed'}
& "$BuildDir/dragon_bond_tests.exe" --review $ReviewDir
if($LASTEXITCODE){throw 'Bond verification failed'}
if($LiveBlink){
    & "$BuildDir/dragon_special_blink_tests.exe" "$ReviewDir/blink" --live
    if($LASTEXITCODE){throw 'Live blink verification failed'}
}
