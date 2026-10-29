param([string]$GameDirectory)
$ErrorActionPreference = 'Stop'
try {
    if (-not $GameDirectory) { $GameDirectory = Read-Host 'Paste the MW2 game folder containing iw4sp.exe' }
    $clean = $GameDirectory.Trim().Trim('"')
    $root = (Resolve-Path -LiteralPath $clean).Path
    $files = @()
    $normal = Join-Path $root 'bo1mw2\logs'
    $fallback = Join-Path $env:LOCALAPPDATA 'BO1MW2'
    if (Test-Path -LiteralPath $normal) { $files += @(Get-ChildItem -LiteralPath $normal -File | Select-Object -ExpandProperty FullName) }
    if (Test-Path -LiteralPath $fallback) { $files += @(Get-ChildItem -LiteralPath $fallback -File | Select-Object -ExpandProperty FullName) }
    $receipt = Join-Path $root 'bo1mw2\install.json'
    if (Test-Path -LiteralPath $receipt) { $files += $receipt }
    # Test40: the model cache manifest (source file sizes/dates; the generated .ff stays behind).
    $modelManifest = Join-Path $root 'bo1mw2\cache\bo1_zombie_models.txt'
    # Test43: and the sound cache manifest.
    $soundManifest = Join-Path $root 'bo1mw2\cache\bo1_zombie_sounds.txt'
    # Test45: the ambience bed length the sound build recorded.
    $soundInfo = Join-Path $root 'bo1mw2\cache\bo1_zombie_sounds.info'
    if ($files.Count -eq 0) { throw 'No logs found. Confirm the DLL is beside iw4sp.exe and run MW2 SP once.' }
    $output = Join-Path $PSScriptRoot ('BO1MW2-test-results-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.zip')
    $stage = Join-Path $env:TEMP ('BO1MW2-logs-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        if (Test-Path -LiteralPath $normal) { Copy-Item -LiteralPath $normal -Destination (Join-Path $stage 'game-logs') -Recurse }
        if (Test-Path -LiteralPath $fallback) { Copy-Item -LiteralPath $fallback -Destination (Join-Path $stage 'fallback-logs') -Recurse }
        if (Test-Path -LiteralPath $receipt) { Copy-Item -LiteralPath $receipt -Destination $stage }
        if (Test-Path -LiteralPath $modelManifest) { Copy-Item -LiteralPath $modelManifest -Destination $stage }
        if (Test-Path -LiteralPath $soundManifest) { Copy-Item -LiteralPath $soundManifest -Destination $stage }
        if (Test-Path -LiteralPath $soundInfo) { Copy-Item -LiteralPath $soundInfo -Destination $stage }
        Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $output
    } finally { Remove-Item -LiteralPath $stage -Recurse -Force }
    Write-Host "Test results: $output"
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
