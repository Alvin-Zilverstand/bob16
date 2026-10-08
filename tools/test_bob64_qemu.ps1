param(
    [Parameter(Mandatory = $true)]
    [string]$QemuPath,
    [Parameter(Mandatory = $true)]
    [string]$OvmfCodePath,
    [string]$ImageRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\bob64-handoff'),
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = 'Stop'
$requiredMarkers = @(
    'bob64: long mode active',
    'bob64: exiting UEFI boot services',
    'bob64 kernel: ring-3 bob! app passed',
    'bob64 kernel: native streaming app passed',
    'bob64 kernel: read-only code protection passed',
    'bob64 kernel: non-executable data protection passed',
    'bob64 kernel: memory checks passed',
    'bob64 kernel: timer IRQ enabled',
    'bob64 kernel: keyboard IRQ enabled',
    'bob64 kernel: PS/2 mouse IRQ enabled',
    'bob64! kernel shell; type help'
)

foreach ($path in @($QemuPath, $OvmfCodePath, $ImageRoot)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required QEMU runtime path does not exist: $path"
    }
}

$efiPath = Join-Path $ImageRoot 'EFI\BOOT\BOOTX64.EFI'
if (-not (Test-Path -LiteralPath $efiPath)) {
    throw "The handoff image is missing: $efiPath"
}

$qemuDirectory = Split-Path -Parent (Resolve-Path -LiteralPath $QemuPath).Path
$shareDirectory = Join-Path $qemuDirectory 'share'
if (-not (Test-Path -LiteralPath $shareDirectory -PathType Container)) {
    throw "QEMU's share directory does not exist: $shareDirectory"
}
$imagePath = (Resolve-Path -LiteralPath $ImageRoot).Path.Replace('\', '/')
$firmwarePath = (Resolve-Path -LiteralPath $OvmfCodePath).Path
$serialPath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-qemu-{0}.log" -f [guid]::NewGuid())

$portProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$monitorPort = ([Net.IPEndPoint]$portProbe.LocalEndpoint).Port
$portProbe.Stop()

$arguments = @(
    '-machine', 'q35', '-m', '512M', '-cpu', 'max', '-L', $shareDirectory,
    '-drive', "if=pflash,format=raw,unit=0,file=$firmwarePath,readonly=on",
    '-drive', "format=raw,file=fat:rw:$imagePath",
    '-boot', 'order=c', '-display', 'none', '-serial', "file:$serialPath",
    '-monitor', "tcp:127.0.0.1:$monitorPort,server=on,wait=off", '-no-reboot'
)

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = (Resolve-Path -LiteralPath $QemuPath).Path
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.Arguments = (($arguments | ForEach-Object {
    '"' + ([string]$_).Replace('"', '\"') + '"'
}) -join ' ')

$process = $null
try {
    $process = [Diagnostics.Process]::Start($startInfo)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $serialText = ''
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serialPath) {
            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
            $allPresent = $true
            foreach ($marker in $requiredMarkers) {
                if ([string]::IsNullOrEmpty($serialText) -or -not $serialText.Contains($marker)) {
                    $allPresent = $false
                    break
                }
            }
            if ($allPresent) {
                Write-Output 'UEFI runtime checks passed:'
                foreach ($marker in $requiredMarkers) {
                    Write-Output "  $marker"
                }
                exit 0
            }
        }
        if ($process.HasExited) {
            throw "QEMU exited before the kernel reached its shell (exit code $($process.ExitCode)).`n$serialText"
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out waiting for the UEFI kernel smoke checks.`n$serialText"
}
finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    if (Test-Path -LiteralPath $serialPath) {
        Remove-Item -LiteralPath $serialPath -Force
    }
}
