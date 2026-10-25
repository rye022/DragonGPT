param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$runtime = Join-Path $PSScriptRoot 'runtime'
if (!(Test-Path -LiteralPath "$runtime/Scripts/python.exe")) {
    & $Python -m venv $runtime
    if ($LASTEXITCODE) { throw 'Could not create an isolated Python runtime.' }
}
$interpreter = Join-Path $runtime 'Scripts/python.exe'
& $interpreter -m pip install --disable-pip-version-check -r "$PSScriptRoot/requirements-tested.txt"
if ($LASTEXITCODE) { throw 'Embedding dependencies could not be installed.' }
& $interpreter "$PSScriptRoot/prepare.py" $PSScriptRoot --python $interpreter
if ($LASTEXITCODE) { throw 'Embedding model preparation failed.' }
Write-Output 'Local CPU embedding is ready. Restart DragonGPT. No user data was uploaded.'
