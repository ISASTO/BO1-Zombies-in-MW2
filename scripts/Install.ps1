param([string]$GameDirectory)
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
    if (-not (Test-Path -LiteralPath $source)) { $source = Join-Path (Split-Path $PSScriptRoot -Parent) 'dist\d3d9.dll' }
    if (-not (Test-Path -LiteralPath $source)) { throw 'd3d9.dll was not found. Extract the full test ZIP first.' }
    $dest = Join-Path $root 'd3d9.dll'
    if (Test-Path -LiteralPath $dest) { throw 'A d3d9.dll already exists here. This installer will not overwrite it. Temporarily move that DLL outside the game folder before this test.' }
    $logs = Join-Path $root 'bo1mw2\logs'
    New-Item -ItemType Directory -Force -Path $logs | Out-Null
    Copy-Item -LiteralPath $source -Destination $dest
    @{
        project = 'BO1MW2'; version = '0.1.0-menu-test1'; executable_sha256 = $hash;
        dll_sha256 = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant();
        installed_utc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'bo1mw2\install.json') -Encoding UTF8
    Write-Host "Installed: $dest"
    Write-Host 'Launch MW2 Single Player normally through Steam. Enter a mission and press F4.'
    Write-Host "Logs: $logs"
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
