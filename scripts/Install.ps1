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
    $version = if ($Build -eq 'startup') { '0.1.1-startup-test2' } else { '0.1.12-spawn-test13' }
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
