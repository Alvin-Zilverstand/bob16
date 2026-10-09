param(
    [Parameter(Mandatory = $true)]
    [string]$QemuPath,
    [Parameter(Mandatory = $true)]
    [string]$OvmfCodePath,
    [Parameter(Mandatory = $true)]
    [string]$OvmfVarsPath,
    [string]$ImageRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\bob64-handoff'),
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
foreach ($path in @($QemuPath, $OvmfCodePath, $OvmfVarsPath, $ImageRoot)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required Bob64 serial test path does not exist: $path"
    }
}

$efiPath = Join-Path $ImageRoot 'EFI\BOOT\BOOTX64.EFI'
if (-not (Test-Path -LiteralPath $efiPath)) {
    throw "The handoff image is missing: $efiPath"
}

function Get-FreeLoopbackPort {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

function Wait-SerialText([Net.Sockets.NetworkStream]$stream,
                         [string]$needle,[int]$startOffset,[int]$timeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($timeoutSeconds)
    $buffer = New-Object byte[] 4096
    while ([DateTime]::UtcNow -lt $deadline) {
        while ($stream.DataAvailable) {
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -le 0) { break }
            [void]$script:serialTranscript.Append(
                [Text.Encoding]::ASCII.GetString($buffer, 0, $count))
        }
        $text = $script:serialTranscript.ToString()
        if ($text.Length -gt $startOffset -and
            $text.Substring($startOffset).Contains($needle)) { return $text }
        if ($script:qemuProcess.HasExited) {
            throw "QEMU exited before COM1 produced '$needle'.`n$text"
        }
        Start-Sleep -Milliseconds 20
    }
    throw "Timed out waiting for COM1 output '$needle'.`n$($script:serialTranscript.ToString())"
}

$imagePath = (Resolve-Path -LiteralPath $ImageRoot).Path.Replace('\', '/')
$firmwarePath = (Resolve-Path -LiteralPath $OvmfCodePath).Path
$varsTemplatePath = (Resolve-Path -LiteralPath $OvmfVarsPath).Path
$varsCopyPath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-serial-vars-{0}.fd" -f [guid]::NewGuid())
Copy-Item -LiteralPath $varsTemplatePath -Destination $varsCopyPath
$serialPort = Get-FreeLoopbackPort
$script:serialTranscript = [Text.StringBuilder]::new()
$script:qemuProcess = $null
$serialClient = $null

try {
    $qemuArguments = @(
        '-machine', 'q35', '-m', '512M', '-cpu', 'max',
        '-drive', "if=pflash,format=raw,unit=0,file=$firmwarePath,readonly=on",
        '-drive', "if=pflash,format=raw,unit=1,file=$varsCopyPath",
        '-drive', "format=raw,file=fat:rw:$imagePath",
        '-boot', 'order=c', '-display', 'none',
        '-serial', "tcp:127.0.0.1:$serialPort,server=on,wait=on",
        '-monitor', 'none'
    )
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Resolve-Path -LiteralPath $QemuPath).Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.Arguments = (($qemuArguments | ForEach-Object {
        '"' + ([string]$_).Replace('"', '\"') + '"'
    }) -join ' ')
    $script:qemuProcess = [Diagnostics.Process]::Start($startInfo)

    $serialClient = $null
    $connectDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not $serialClient -and [DateTime]::UtcNow -lt $connectDeadline) {
        $candidate = [Net.Sockets.TcpClient]::new()
        try {
            $candidate.Connect('127.0.0.1', $serialPort)
            $serialClient = $candidate
        } catch {
            $candidate.Dispose()
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $serialClient) {
        throw 'Could not connect to QEMU COM1.'
    }
    $stream = $serialClient.GetStream()
    $stream.ReadTimeout = 100
    [void](Wait-SerialText $stream 'bob64> ' 0 $TimeoutSeconds)
    if (-not $script:serialTranscript.ToString().Contains(
            'bob64 kernel: serial RX IRQ enabled')) {
        throw "The kernel did not enable COM1 receive interrupts.`n$($script:serialTranscript.ToString())"
    }

    $startOffset = $script:serialTranscript.Length
    $commandBytes = [Text.Encoding]::ASCII.GetBytes("version`r")
    $stream.Write($commandBytes, 0, $commandBytes.Length)
    $stream.Flush()
    [void](Wait-SerialText $stream 'bob64 kernel 0.1' $startOffset $TimeoutSeconds)

    $startOffset = $script:serialTranscript.Length
    $commandBytes = [Text.Encoding]::ASCII.GetBytes("write serial.txt bob!`r")
    $stream.Write($commandBytes, 0, $commandBytes.Length)
    $stream.Flush()
    [void](Wait-SerialText $stream 'saved' $startOffset $TimeoutSeconds)

    $startOffset = $script:serialTranscript.Length
    $commandBytes = [Text.Encoding]::ASCII.GetBytes("cat serial.txt`r")
    $stream.Write($commandBytes, 0, $commandBytes.Length)
    $stream.Flush()
    [void](Wait-SerialText $stream "bob!`r`nbob64> " $startOffset $TimeoutSeconds)

    Write-Output 'COM1 IRQ runtime check passed: version, file write, and file read commands completed over serial.'
}
finally {
    if ($serialClient) { $serialClient.Dispose() }
    if ($script:qemuProcess -and -not $script:qemuProcess.HasExited) {
        $script:qemuProcess.Kill()
        $script:qemuProcess.WaitForExit()
    }
    if (Test-Path -LiteralPath $varsCopyPath) {
        Remove-Item -LiteralPath $varsCopyPath -Force
    }
}
