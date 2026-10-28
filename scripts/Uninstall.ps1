param([string]$GameDirectory)
$ErrorActionPreference = 'Stop'
try {
    if (-not $GameDirectory) { $GameDirectory = Read-Host 'Paste the MW2 game folder containing iw4sp.exe' }
    $clean = $GameDirectory.Trim().Trim('"')
    $root = (Resolve-Path -LiteralPath $clean).Path
    if (Get-Process iw4sp,iw4mp -ErrorAction SilentlyContinue) { throw 'Close MW2 before removing the mod.' }
    $dll = Join-Path $root 'd3d9.dll'
    $receipt = Join-Path $root 'bo1mw2\install.json'
    if (-not (Test-Path -LiteralPath $dll)) { Write-Host 'No d3d9.dll found.'; exit 0 }
    if (-not (Test-Path -LiteralPath $receipt)) { throw 'No installation receipt. For a manual installation, remove only the d3d9.dll copied from this project.' }
    $manifest = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    if ($manifest.project -ne 'BO1MW2' -or (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.dll_sha256) {
        throw 'The installed DLL differs from the installation receipt. Nothing was removed.'
    }
    Remove-Item -LiteralPath $dll
    Remove-Item -LiteralPath $receipt
    # Test40: the zombie models generated from the Black Ops install.
    $cache = Join-Path $root 'bo1mw2\cache'
    if (Test-Path -LiteralPath $cache -PathType Container) {
        # Test43: and the zombie sounds.
        foreach ($name in @('bo1_zombie_models.ff','bo1_zombie_models.txt','bo1_zombie_models.loading','bo1_zombie_models.ff.tmp','bo1_zombie_models.txt.tmp',
                            'bo1_zombie_sounds.ff','bo1_zombie_sounds.txt','bo1_zombie_sounds.loading','bo1_zombie_sounds.ff.tmp','bo1_zombie_sounds.txt.tmp')) {
            $file = Join-Path $cache $name
            if (Test-Path -LiteralPath $file -PathType Leaf) { Remove-Item -LiteralPath $file }
        }
        if (-not (Get-ChildItem -LiteralPath $cache -Force)) { Remove-Item -LiteralPath $cache }
    }
    Write-Host 'Mod removed. Logs were kept; original game files were unchanged.'
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
