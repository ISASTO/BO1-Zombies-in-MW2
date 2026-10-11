param([string]$GameDirectory)
$ErrorActionPreference = 'Stop'
try {
    if (-not $GameDirectory) { $GameDirectory = Read-Host 'Paste the MW2 game folder containing iw4sp.exe' }
    $root = (Resolve-Path -LiteralPath $GameDirectory.Trim().Trim('"')).Path
    if (-not (Test-Path -LiteralPath (Join-Path $root 'iw4sp.exe') -PathType Leaf)) { throw 'This folder does not contain iw4sp.exe.' }
    if (Get-Process iw4sp,iw4mp -ErrorAction SilentlyContinue) { throw 'Close MW2 before collecting startup diagnostics.' }
    $probe = Join-Path $PSScriptRoot 'LoaderProbe.exe'
    if (-not (Test-Path -LiteralPath $probe)) { $probe = Join-Path (Split-Path $PSScriptRoot -Parent) 'dist\startup\LoaderProbe.exe' }
    if (-not (Test-Path -LiteralPath $probe)) { throw 'LoaderProbe.exe is missing. Extract the complete startup-test ZIP first.' }
    $id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $stage = Join-Path $PSScriptRoot ('startup-results-' + $id)
    $output = Join-Path $PSScriptRoot ('BO1MW2-startup-results-' + $id + '.zip')
    New-Item -ItemType Directory -Path $stage | Out-Null
    $normal = Join-Path $root 'bo1mw2\logs'
    $fallback = Join-Path $env:LOCALAPPDATA 'BO1MW2'
    $tempFallback = Join-Path $env:TEMP 'BO1MW2\startup'
    $receiptPath = Join-Path $root 'bo1mw2\install.json'
    # Preserve the failing run before any probe can initialize a DLL.
    if (Test-Path -LiteralPath $normal) { Copy-Item -LiteralPath $normal -Destination (Join-Path $stage 'game-logs') -Recurse }
    if (Test-Path -LiteralPath $fallback) { Copy-Item -LiteralPath $fallback -Destination (Join-Path $stage 'prior-fallback-logs') -Recurse }
    if (Test-Path -LiteralPath $receiptPath) { Copy-Item -LiteralPath $receiptPath -Destination $stage }
    $info = [ordered]@{
        collected_utc = [DateTime]::UtcNow.ToString('o'); windows = [Environment]::OSVersion.VersionString;
        os_64bit = [Environment]::Is64BitOperatingSystem; powershell_64bit = [Environment]::Is64BitProcess;
        expected_executable_sha256 = 'fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777';
        files = @(); probes = @()
    }
    foreach ($name in @('iw4sp.exe','d3d9.dll','steam_api64.dll','bink2w64.dll')) {
        $path = Join-Path $root $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $item = Get-Item -LiteralPath $path
            $info.files += @{ name = $name; bytes = $item.Length; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
        } else { $info.files += @{ name = $name; missing = $true } }
    }
    try {
        $info.windows_details = Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture
        $info.gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,Status)
    } catch { $info.hardware_query_error = $_.Exception.Message }
    try {
        $events = @(Get-WinEvent -FilterHashtable @{ LogName='Application'; StartTime=(Get-Date).AddHours(-24); Id=1000,1001,1002,33,35,59 } -ErrorAction Stop |
            Where-Object { $_.Message -match '(?i)\biw4sp\.exe\b|\bLoaderProbe\.exe\b|BO1MW2' } |
            Select-Object TimeCreated,Id,ProviderName,Message)
        ConvertTo-Json -InputObject $events -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'windows-events.json') -Encoding UTF8
    } catch { $_.Exception.Message | Set-Content -LiteralPath (Join-Path $stage 'windows-events-unavailable.txt') -Encoding UTF8 }
    # Each probe is a fresh process; loaded modules cannot affect the next probe.
    $targets = @(@{ name = 'windows-system-d3d9'; path = (Join-Path $env:WINDIR 'System32\d3d9.dll') })
    $installed = Join-Path $root 'd3d9.dll'
    if (Test-Path -LiteralPath $installed -PathType Leaf) {
        $dllHash = (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash.ToLowerInvariant()
        $ours = $dllHash -eq '662695d7112452fc58a4da23a1a682b537e601dd55eedd6b42ca118d774a92a9'
        if (Test-Path -LiteralPath $receiptPath) {
            try {
                $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
                $ours = $ours -or ($receipt.project -eq 'BO1MW2' -and $receipt.dll_sha256 -eq $dllHash)
            } catch { $info.receipt_error = $_.Exception.Message }
        }
        if ($ours) { $targets += @{ name = 'installed-project-dll'; path = $installed } }
        else { $info.installed_probe_skipped = 'DLL is not identified as this project. It was not loaded.' }
    }
    $control = Join-Path $PSScriptRoot 'd3d9.dll'
    $payloadPath = Join-Path $PSScriptRoot 'payload.json'
    if ((Test-Path -LiteralPath $control) -and (Test-Path -LiteralPath $payloadPath)) {
        $payload = Get-Content -LiteralPath $payloadPath -Raw | ConvertFrom-Json
        if ($payload.build -eq 'startup' -and $payload.dll_sha256 -eq (Get-FileHash -LiteralPath $control -Algorithm SHA256).Hash.ToLowerInvariant()) {
            $targets += @{ name = 'packaged-startup-control'; path = $control }
        }
    }
    foreach ($target in $targets) {
        $log = Join-Path $stage ($target.name + '.log')
        Write-Host ('Checking ' + $target.name + '...')
        try {
            $arguments = '"' + $target.path + '" "' + $log + '"'
            $process = Start-Process -FilePath $probe -ArgumentList $arguments -PassThru -WorkingDirectory $PSScriptRoot
            $process.Handle | Out-Null
            if ($process.WaitForExit(15000)) {
                $process.WaitForExit()
                $info.probes += @{ name = $target.name; exit_code = $process.ExitCode }
            } else {
                $process.Kill()
                $info.probes += @{ name = $target.name; timeout = '15 seconds; stopped only our probe process' }
            }
        } catch { $info.probes += @{ name = $target.name; error = $_.Exception.Message } }
    }
    $startup = Join-Path $fallback 'startup'
    if (Test-Path -LiteralPath $startup) { Copy-Item -LiteralPath $startup -Destination (Join-Path $stage 'startup-logs') -Recurse }
    if (Test-Path -LiteralPath $tempFallback) { Copy-Item -LiteralPath $tempFallback -Destination (Join-Path $stage 'temp-startup-logs') -Recurse }
    $info | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $stage 'startup-info.json') -Encoding UTF8
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $output
    Remove-Item -LiteralPath $stage -Recurse -Force
    Write-Host "Attach this ZIP: $output"
    Write-Host 'Also report whether normal Steam launch works after uninstalling our DLL, and with the startup control installed.'
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
