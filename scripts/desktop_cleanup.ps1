param(
    [Parameter(Mandatory=$true)][string]$PlanPath,
    [ValidateSet('Preview','Move','Restore')][string]$Action='Preview',
    [switch]$Confirmed
)
$ErrorActionPreference='Stop'
$plan=Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
if($Action -eq 'Preview') { $plan.files | Select-Object source,destination,bytes,sha256; exit 0 }
if(!$Confirmed) { throw 'Review the exact plan and obtain user confirmation before using -Confirmed.' }
$desktop=[IO.Path]::GetFullPath([Environment]::GetFolderPath('Desktop')).TrimEnd('\')
$archive=[IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'DragonGPT\Releases\archive')).TrimEnd('\')
$operations=@()
foreach($row in $plan.files) {
    $original=[IO.Path]::GetFullPath($row.source)
    $saved=[IO.Path]::GetFullPath($row.destination)
    if([IO.Path]::GetDirectoryName($original) -ne $desktop -or [IO.Path]::GetDirectoryName($saved) -ne $archive) { throw 'Plan paths are outside the reviewed desktop/archive directories.' }
    if([IO.Path]::GetFileName($original) -notmatch '^DragonGPT-v[0-9.]+-Windows-x64\.zip$' -or [IO.Path]::GetFileName($saved) -ne [IO.Path]::GetFileName($original)) { throw 'Unexpected archive name.' }
    $source=if($Action -eq 'Restore'){$saved}else{$original}
    $target=if($Action -eq 'Restore'){$original}else{$saved}
    if(!(Test-Path -LiteralPath $source -PathType Leaf) -or (Test-Path -LiteralPath $target)) { throw 'Source is missing or destination is occupied; nothing was overwritten.' }
    if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $row.sha256) { throw 'Archive changed since preview.' }
    $operations+=@{source=$source;target=$target;sha256=$row.sha256}
}
$journalPath=[IO.Path]::GetFullPath($PlanPath)+'.operations.json'
$journal=@()
foreach($operation in $operations) {
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($operation.target)) | Out-Null
    Move-Item -LiteralPath $operation.source -Destination $operation.target -ErrorAction Stop
    if((Get-FileHash -LiteralPath $operation.target -Algorithm SHA256).Hash -ne $operation.sha256) { throw 'Post-move verification failed; preserve both the plan and journal for recovery.' }
    $journal+=@{action=$Action;source=$operation.source;destination=$operation.target;sha256=$operation.sha256;at=(Get-Date).ToUniversalTime().ToString('o')}
    $journal | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $journalPath -Encoding utf8
}
Write-Output "$Action completed and verified. No files were deleted. Journal: $journalPath"
