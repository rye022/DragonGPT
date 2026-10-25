param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$BuildDir = "$PSScriptRoot/build",
    [Parameter(Mandatory=$true)][string]$CompilerDir,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$qtResolved = (Resolve-Path -LiteralPath $QtRoot).Path
if (!(Test-Path -LiteralPath "$qtResolved/lib/cmake/Qt6/Qt6Config.cmake")) { throw 'QtRoot must contain a Qt6 MinGW SDK.' }
$env:PATH = "$qtResolved/bin;$CompilerDir;$env:PATH"
cmake -S $PSScriptRoot -B $BuildDir -G Ninja "-DCMAKE_PREFIX_PATH=$qtResolved" "-DCMAKE_CXX_COMPILER=$CompilerDir/g++.exe" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'CMake configure failed' }
cmake --build $BuildDir --parallel 4
if ($LASTEXITCODE) { throw 'Compilation failed' }
ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
if ($Package) {
    $destination = "$PSScriptRoot/dist/DragonGPT"
    New-Item -ItemType Directory -Force $destination | Out-Null
    Copy-Item -LiteralPath "$BuildDir/DragonGPT.exe" -Destination $destination
    # Fixed deployment manifest for this Qt Widgets + SQLite application.
    foreach ($dll in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Sql.dll','Qt6Network.dll','Qt6Multimedia.dll','Qt6WebSockets.dll','Qt6Concurrent.dll')) {
        Copy-Item -LiteralPath "$qtResolved/bin/$dll" -Destination $destination
    }
    foreach ($dll in @('libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')) {
        Copy-Item -LiteralPath "$CompilerDir/$dll" -Destination $destination
    }
    $pluginFiles = @('platforms/qwindows.dll','platforms/qoffscreen.dll','sqldrivers/qsqlite.dll','styles/qmodernwindowsstyle.dll','tls/qschannelbackend.dll','multimedia/windowsmediaplugin.dll')
    foreach ($plugin in $pluginFiles) {
        $targetDir = Join-Path $destination (Split-Path -Parent $plugin)
        New-Item -ItemType Directory -Force $targetDir | Out-Null
        Copy-Item -LiteralPath "$qtResolved/plugins/$plugin" -Destination $targetDir
    }
    Set-Content -LiteralPath "$destination/qt.conf" -Value "[Paths]`nPlugins=." -Encoding ascii
    if (Test-Path -LiteralPath "$PSScriptRoot/README.md") {
    Copy-Item -LiteralPath "$PSScriptRoot/README.md" -Destination $destination
    }
    Copy-Item -LiteralPath "$PSScriptRoot/THIRD_PARTY.md" -Destination $destination
    Copy-Item -LiteralPath "$PSScriptRoot/licenses" -Destination $destination -Recurse -Force
    Copy-Item -LiteralPath "$PSScriptRoot/docs" -Destination $destination -Recurse -Force
    Copy-Item -LiteralPath "$PSScriptRoot/prompts" -Destination $destination -Recurse -Force
    Copy-Item -LiteralPath "$PSScriptRoot/assets" -Destination $destination -Recurse -Force
    Copy-Item -LiteralPath "$PSScriptRoot/offline" -Destination $destination -Recurse -Force
    $embeddingTarget = Join-Path $destination 'embedding'
    New-Item -ItemType Directory -Force $embeddingTarget | Out-Null
    foreach ($file in @('worker.py','prepare.py','install.ps1','requirements-tested.txt')) {
        Copy-Item -LiteralPath "$PSScriptRoot/embedding/$file" -Destination $embeddingTarget -Force
    }
    if (Test-Path -LiteralPath "$PSScriptRoot/embedding/model") {
        Copy-Item -LiteralPath "$PSScriptRoot/embedding/model" -Destination $embeddingTarget -Recurse -Force
    }
    Copy-Item -LiteralPath "$PSScriptRoot/install.ps1" -Destination $destination
    Copy-Item -LiteralPath "$PSScriptRoot/uninstall.ps1" -Destination $destination
    Copy-Item -LiteralPath "$PSScriptRoot/Start-Desktop.cmd" -Destination $destination
    Write-Output "Packaged app: $destination/DragonGPT.exe"
}
