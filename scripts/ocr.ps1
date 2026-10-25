$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType=WindowsRuntime] | Out-Null
    [Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType=WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.InMemoryRandomAccessStream, Windows.Foundation, ContentType=WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.DataWriter, Windows.Foundation, ContentType=WindowsRuntime] | Out-Null
    function Await-WinRT($Operation, $Type) {
        $method = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetGenericArguments().Count -eq 1 -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
        $task = $method.MakeGenericMethod($Type).Invoke($null, @($Operation))
        if (!$task.Wait(15000)) { throw 'OCR operation timed out' }
        return $task.Result
    }
    $payload = [Console]::In.ReadToEnd() | ConvertFrom-Json
    $bytes = [Convert]::FromBase64String($payload.png)
    if ($bytes.Length -gt 12000000) { throw 'Image exceeds OCR limit' }
    $stream = New-Object Windows.Storage.Streams.InMemoryRandomAccessStream
    $writer = New-Object Windows.Storage.Streams.DataWriter($stream.GetOutputStreamAt(0))
    $writer.WriteBytes($bytes)
    $null = Await-WinRT $writer.StoreAsync() ([UInt32])
    $stream.Seek(0)
    $decoder = Await-WinRT ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await-WinRT ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
    if ($null -eq $engine) { throw 'No Windows OCR language installed' }
    $result = Await-WinRT ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
    @{ok=$true; text=$result.Text; language=$engine.RecognizerLanguage.LanguageTag} | ConvertTo-Json -Compress
    $bitmap.Dispose(); $writer.Dispose(); $stream.Dispose()
} catch {
    @{ok=$false; error='Windows OCR unavailable or failed. Install a Windows OCR language, or configure an image-capable model.'} | ConvertTo-Json -Compress
    exit 1
}
