param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$CompilerDir,
    [string]$BuildDir="$PSScriptRoot/build/recheck",
    [string]$ReviewDir="$PSScriptRoot/build/blink-review",
    [switch]$Live
)
$ErrorActionPreference='Stop'
$env:PATH="$QtRoot/bin;$CompilerDir;$env:PATH"
$env:QT_QPA_PLATFORM='windows'
cmake -S $PSScriptRoot -B $BuildDir -G Ninja "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$CompilerDir/g++.exe" -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE){throw 'Configure failed'}
cmake --build $BuildDir --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
ctest --test-dir $BuildDir --output-on-failure
if($LASTEXITCODE){throw 'Regression test failed'}
New-Item -ItemType Directory -Force -Path $ReviewDir | Out-Null
if($Live){& "$BuildDir/dragon_special_blink_tests.exe" $ReviewDir --live}
else{& "$BuildDir/dragon_special_blink_tests.exe" $ReviewDir}
if($LASTEXITCODE){throw 'Blink verification failed'}
