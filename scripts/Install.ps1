param([string]$GameDirectory, [ValidateSet('menu','startup')][string]$Build = 'menu')
$ErrorActionPreference = 'Stop'
try {
    if (-not $GameDirectory) { $GameDirectory = Read-Host 'Paste the MW2 game folder containing iw4sp.exe' }
    $GameDirectory = $GameDirectory.Trim().Trim('"')
    $root = (Resolve-Path -LiteralPath $GameDirectory).Path
    $exe = Join-Path $root 'iw4sp.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'This folder does not contain iw4sp.exe.' }
    if (Get-Process iw4sp,iw4mp -ErrorAction SilentlyContinue) { throw 'Close MW2 before installing the mod.' }
    $expected = 'fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777'
    $hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $expected) { throw "This build is unsupported. Expected SHA256 $expected; got $hash. Send this hash before installing." }
    $source = Join-Path $PSScriptRoot 'd3d9.dll'
    if (-not (Test-Path -LiteralPath $source)) {
        $relative = if ($Build -eq 'startup') { 'dist\startup\d3d9.dll' } else { 'dist\d3d9.dll' }
        $source = Join-Path (Split-Path $PSScriptRoot -Parent) $relative
    }
    if (-not (Test-Path -LiteralPath $source)) { throw 'd3d9.dll was not found. Extract the full test ZIP first.' }
    $version = if ($Build -eq 'startup') { '0.1.1-startup-test2' } else { '0.1.22-squad-test23' }
    $payloadPath = Join-Path $PSScriptRoot 'payload.json'
    if (Test-Path -LiteralPath $payloadPath) {
        $payload = Get-Content -LiteralPath $payloadPath -Raw | ConvertFrom-Json
        if ($payload.build -ne $Build -or $payload.version -ne $version -or
            $payload.dll_sha256 -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()) {
            throw 'The selected build does not match this package. Extract the complete ZIP and use its supplied installer.'
        }
    }
    $dest = Join-Path $root 'd3d9.dll'
    if (Test-Path -LiteralPath $dest) { throw 'A d3d9.dll already exists here. Run Uninstall.cmd for our previous build first. This installer will not overwrite an existing wrapper.' }
    $logs = Join-Path $root 'bo1mw2\logs'
    New-Item -ItemType Directory -Force -Path $logs | Out-Null
    $players = Join-Path $root 'players'
    if ($Build -eq 'menu' -and (Test-Path -LiteralPath $players -PathType Container)) {
        $saveBackup = Join-Path $root ('bo1mw2\backups\before-test23-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
        New-Item -ItemType Directory -Path $saveBackup | Out-Null
        Copy-Item -LiteralPath $players -Destination $saveBackup -Recurse
        Write-Host "Local player/save backup: $saveBackup"
    }
    if ($Build -eq 'menu') {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $assetDirectory = Join-Path $root 'bo1mw2\assets'
        New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null
        $asset = Join-Path $assetDirectory 'damage_feedback.iwi'
        $found = $false
        $archives = @(Get-ChildItem -LiteralPath (Join-Path $root 'main') -Filter '*.iwd' -File -ErrorAction SilentlyContinue | Sort-Object Name -Descending)
        foreach ($archive in $archives) {
            $zip = [IO.Compression.ZipFile]::OpenRead($archive.FullName)
            try {
                $entry = $zip.Entries | Where-Object { $_.FullName.Replace('\','/') -ieq 'images/damage_feedback.iwi' } | Select-Object -First 1
                if (-not $entry -or $entry.Length -lt 32 -or $entry.Length -gt 65536) { continue }
                $stream = $entry.Open()
                $memory = New-Object IO.MemoryStream
                try { $stream.CopyTo($memory); $bytes = $memory.ToArray() } finally { $stream.Dispose(); $memory.Dispose() }
                if ($bytes.Length -ne $entry.Length -or $bytes[0] -ne 73 -or $bytes[1] -ne 87 -or $bytes[2] -ne 105 -or
                    $bytes[3] -ne 8 -or $bytes[8] -ne 13 -or [BitConverter]::ToUInt16($bytes,10) -ne 36 -or
                    [BitConverter]::ToUInt16($bytes,12) -ne 72 -or [BitConverter]::ToUInt16($bytes,14) -ne 1 -or
                    [BitConverter]::ToUInt32($bytes,16) -ne $bytes.Length -or $bytes.Length -lt 2624) { continue }
                [IO.File]::WriteAllBytes($asset + '.tmp',$bytes)
                Move-Item -LiteralPath ($asset + '.tmp') -Destination $asset -Force
                $found = $true
                Write-Host ('Original MP hitmarker extracted from ' + $archive.Name)
                break
            } finally { $zip.Dispose() }
        }
        if (-not $found) {
            if (Test-Path -LiteralPath $asset) { Remove-Item -LiteralPath $asset }
            Write-Host 'Original MP hitmarker image was not found in main/*.iwd. Hitmarkers will remain unavailable.'
        }
    }
    Copy-Item -LiteralPath $source -Destination $dest
    @{
        project = 'BO1MW2'; version = $version; build = $Build; executable_sha256 = $hash;
        dll_sha256 = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant();
        installed_utc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'bo1mw2\install.json') -Encoding UTF8
    Write-Host "Installed: $dest"
    if ($Build -eq 'startup') {
        Write-Host 'Startup control installed. Launch MW2 SP through Steam and try loading a mission.'
        Write-Host 'This diagnostic build has no menu or F4 action. Run Diagnose.cmd after the test.'
        Write-Host "Startup logs: $env:LOCALAPPDATA\BO1MW2\startup"
    } else {
        Write-Host 'Launch MW2 Single Player normally through Steam. Enter a mission and press F4.'
        Write-Host "Logs: $logs"
    }
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
