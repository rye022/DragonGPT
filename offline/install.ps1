param([string]$Python = 'python', [string]$EnvironmentDirectory = "$env:LOCALAPPDATA/DragonGPT/tts-venv")
$ErrorActionPreference = 'Stop'
$runtime = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../offline-runtime'))
New-Item -ItemType Directory -Force $runtime | Out-Null
$interpreter = Join-Path $EnvironmentDirectory 'Scripts/python.exe'
if (!(Test-Path -LiteralPath $interpreter)) {
    & $Python -m venv $EnvironmentDirectory
    if ($LASTEXITCODE) { throw 'Python 3.12 x64 is required.' }
}
& $interpreter -m pip install --disable-pip-version-check torch==2.11.0+cu128 --index-url https://download.pytorch.org/whl/cu128
if ($LASTEXITCODE) { throw 'PyTorch installation failed.' }
& $interpreter -m pip install --disable-pip-version-check qwen-tts==0.1.1 gradio==6.17.3 huggingface-hub==0.36.2 torchaudio==2.11.0
if ($LASTEXITCODE) { throw 'Qwen-TTS installation failed.' }
& $interpreter (Join-Path $PSScriptRoot 'prepare.py') --runtime $runtime
if ($LASTEXITCODE) { throw 'Offline model preparation failed.' }
Write-Output 'Offline Serena is ready. Restart DragonGPT to refresh the installed status.'
