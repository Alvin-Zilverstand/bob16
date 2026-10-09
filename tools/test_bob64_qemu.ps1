param(
    [Parameter(Mandatory = $true)]
    [string]$QemuPath,
    [Parameter(Mandatory = $true)]
    [string]$OvmfCodePath,
    [string]$OvmfVarsPath,
    [string]$ImageRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\bob64-handoff'),
    [int]$TimeoutSeconds = 45,
    [switch]$ProbeOnly,
    [switch]$DiskSnapshotTest,
    [switch]$LargeLbaDiskSnapshotTest,
    [switch]$ExpectStorageWrite,
    [ValidateSet('keyboard','mouse','both','many','disconnect','hub','storage','storage-hotplug','storage-active-disconnect','storage64')]
    [string]$ProbeDevice = 'keyboard'
)

$ErrorActionPreference = 'Stop'
if ($DiskSnapshotTest -and $LargeLbaDiskSnapshotTest) {
    throw 'Choose either -DiskSnapshotTest or -LargeLbaDiskSnapshotTest.'
}
if ($LargeLbaDiskSnapshotTest) { $DiskSnapshotTest = $true }
$requiredMarkers = @(
    'bob64: long mode active',
    'bob64: exiting UEFI boot services',
    'bob64 kernel: dynamic page-table pool passed',
    'bob64 kernel: ring-3 bob! app passed',
    'bob64 kernel: native streaming app passed',
    'bob64 kernel: nested app return passed',
    'bob64 kernel: native echo arguments passed',
    'bob64 kernel: native file-read app passed',
    'bob64 kernel: native file-list app passed',
    'bob64 kernel: large notes streaming copy passed',
    'bob64 kernel: native notes app passed',
    'bob64 kernel: native info app passed',
    'bob64 kernel: full-width user exception diagnostics passed',
    'r15=0x1234567887654321',
    'rsp=0x0000004002',
    'ss=0x000000000000001b',
    'cr2=0x0000004000000000',
    'bob64 kernel: read-only code protection passed',
    'bob64 kernel: non-executable data protection passed',
    'bob64 kernel: memory checks passed',
    'bob64 kernel: xHCI PCI controller found at',
    'bob64 kernel: PCI enumeration passed',
    'bob64 kernel: xHCI capabilities mapped, version=',
    'bob64 kernel: xHCI controller halted and reset',
    'bob64 kernel: xHCI command ring completion passed',
    'bob64 kernel: timer IRQ enabled',
    'bob64 kernel: timer tick passed',
    'bob64 kernel: serial RX IRQ enabled',
    'bob64 kernel: keyboard IRQ enabled',
    'bob64 kernel: PS/2 mouse IRQ enabled',
    'bob64! kernel shell; type help'
)
$persistentMarker = 'bob64 kernel: persistent B64S smoke file passed'
$mouseMarker = 'bob! live mouse move passed'
$mouseWheelMarker = 'bob! live mouse wheel passed'
$mouseButtonMarker = 'bob! live mouse button passed'
$shellMarker = 'bob64! kernel shell; type help'
$shellPromptMarker = 'bob64> '

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
$shareCandidates = @(
    (Join-Path $qemuDirectory 'share'),
    (Join-Path $qemuDirectory '..\share\qemu'),
    (Join-Path $qemuDirectory '..\share')
)
$shareDirectory = $null
foreach ($candidate in $shareCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        $shareDirectory = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}
if (-not $shareDirectory) {
    throw "QEMU's share directory was not found beside or above its executable: $qemuDirectory"
}
$imagePath = (Resolve-Path -LiteralPath $ImageRoot).Path.Replace('\', '/')
$firmwarePath = (Resolve-Path -LiteralPath $OvmfCodePath).Path
$firmwareDirectory = Split-Path -Parent (Resolve-Path -LiteralPath $OvmfCodePath).Path
if ([string]::IsNullOrWhiteSpace($OvmfVarsPath)) {
    foreach ($candidate in @('OVMF_VARS.fd', 'OVMF_VARS_4M.fd',
                             'edk2-x86_64-vars.fd', 'edk2-i386-vars.fd')) {
        $candidatePath = Join-Path $firmwareDirectory $candidate
        if (Test-Path -LiteralPath $candidatePath) {
            $OvmfVarsPath = $candidatePath
            break
        }
    }
}
if ([string]::IsNullOrWhiteSpace($OvmfVarsPath) -or
    -not (Test-Path -LiteralPath $OvmfVarsPath)) {
    throw 'A writable OVMF variable-store template is required; pass -OvmfVarsPath.'
}
$varsTemplatePath = (Resolve-Path -LiteralPath $OvmfVarsPath).Path
$serialPath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-qemu-{0}.log" -f [guid]::NewGuid())
$framebufferPath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-framebuffer-{0}.ppm" -f [guid]::NewGuid())
$varsCopyPath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-ovmf-vars-{0}.fd" -f [guid]::NewGuid())
$storageImagePath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-usb-storage-{0}.img" -f [guid]::NewGuid())
$storageReconnectImagePath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-usb-storage-reconnect-{0}.img" -f [guid]::NewGuid())
$diskSnapshotImagePath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-disk-snapshot-{0}.img" -f [guid]::NewGuid())
Copy-Item -LiteralPath $varsTemplatePath -Destination $varsCopyPath
if ($DiskSnapshotTest) {
    if ($ProbeOnly) { throw '-DiskSnapshotTest requires the full shell regression run.' }
    if ($LargeLbaDiskSnapshotTest) {
        & (Join-Path $PSScriptRoot 'new_bob64_disk.ps1') `
            -Path $diskSnapshotImagePath -SizeMiB 2097154 `
            -FirstPartitionLba 4294969344 -Sparse | Out-Null
    } else {
        & (Join-Path $PSScriptRoot 'new_bob64_disk.ps1') `
            -Path $diskSnapshotImagePath -SizeMiB 64 | Out-Null
    }
}
if ($ProbeOnly -and $ProbeDevice -eq 'storage64') {
    [IO.File]::Open($storageImagePath,[IO.FileMode]::CreateNew).Dispose()
    & fsutil.exe sparse setflag $storageImagePath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not mark the temporary large-LBA test image as sparse.'
    }
    $storageImage = [IO.File]::Open($storageImagePath,[IO.FileMode]::Open,
        [IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {
        $highLba = [long]4294967296
        $storageImage.SetLength(($highLba + 8) * 512)
        $storageImage.Position = $highLba * 512
        $highLbaPattern = [byte[]]::new(512)
        for ($index=0; $index -lt $highLbaPattern.Length; $index++) {
            $highLbaPattern[$index] = [byte](($index * 53 + 0xa7) -band 0xff)
        }
        $storageImage.Write($highLbaPattern,0,$highLbaPattern.Length)
        $storageImage.Flush($true)
    }
    finally { $storageImage.Dispose() }
} elseif ($ProbeOnly -and $ProbeDevice -in @('storage','storage-hotplug','storage-active-disconnect')) {
    $storageImage = [IO.File]::Open($storageImagePath,[IO.FileMode]::CreateNew)
    try { $storageImage.SetLength(16MB) }
    finally { $storageImage.Dispose() }
    if ($ProbeDevice -eq 'storage-hotplug') {
        $storageReconnectImage = [IO.File]::Open($storageReconnectImagePath,[IO.FileMode]::CreateNew)
        try { $storageReconnectImage.SetLength(16MB) }
        finally { $storageReconnectImage.Dispose() }
    }
}

$portProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$monitorPort = ([Net.IPEndPoint]$portProbe.LocalEndpoint).Port
$portProbe.Stop()

function Send-MonitorCommand([string]$command,[switch]$Capture) {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $monitorPort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 3000
        $buffer = New-Object byte[] 1024
        $response = [Text.StringBuilder]::new()
        while (-not $response.ToString().Contains('(qemu)')) {
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -le 0) { break }
            [void]$response.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $count))
        }
        $bytes = [Text.Encoding]::ASCII.GetBytes($command + "`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        $response.Clear() | Out-Null
        while (-not $response.ToString().Contains('(qemu)')) {
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -le 0) { break }
            [void]$response.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $count))
        }
        if ($Capture) { return $response.ToString() }
    }
    finally {
        $client.Dispose()
    }
}

function Send-GuestText([string]$text,[int]$delayMilliseconds=125) {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $monitorPort)
        $stream = $client.GetStream()
        foreach ($character in $text.ToCharArray()) {
            if ($character -eq ' ') { $key = 'spc' }
            elseif ($character -eq '.') { $key = 'dot' }
            else { $key = [string]$character }
            $bytes = [Text.Encoding]::ASCII.GetBytes("sendkey $key`n")
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
            Start-Sleep -Milliseconds $delayMilliseconds
        }
        $bytes = [Text.Encoding]::ASCII.GetBytes("sendkey ret`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        Start-Sleep -Milliseconds 250
    }
    finally {
        $client.Dispose()
    }
}

function Stop-QemuForStorageCheck {
    if ($process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
}

function Assert-StorageTestPattern([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    try {
        if ($stream.Length -lt 1024) {
            throw "QEMU storage test image is too short: $path"
        }
        [void]$stream.Seek(512,[IO.SeekOrigin]::Begin)
        for ($index=0; $index -lt 512; $index++) {
            $expected = ($index * 37 + 0x5a) -band 0xff
            if ($stream.ReadByte() -ne $expected) {
                throw "SYNCHRONIZE CACHE did not persist the expected bytes at LBA 1 in $path (byte $index)."
            }
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Assert-FramebufferRendered([string]$path) {
    $qemuPath = $path.Replace('\', '/')
    $response = Send-MonitorCommand "screendump $qemuPath" -Capture
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "QEMU did not produce the framebuffer screenshot. Monitor response: $response"
    }
    $bytes = [IO.File]::ReadAllBytes($path)
    $headerLength = [Math]::Min($bytes.Length, 128)
    $headerText = [Text.Encoding]::ASCII.GetString($bytes, 0, $headerLength)
    $header = [regex]::Match($headerText, '^P6\s+(\d+)\s+(\d+)\s+(\d+)\s')
    if (-not $header.Success) {
        throw 'QEMU framebuffer screenshot did not contain a valid binary PPM header.'
    }
    $width = [int]$header.Groups[1].Value
    $height = [int]$header.Groups[2].Value
    $maximum = [int]$header.Groups[3].Value
    $dataOffset = $header.Length
    if ($width -lt 320 -or $height -lt 200 -or $maximum -ne 255 -or
        $bytes.Length - $dataOffset -ne $width * $height * 3) {
        throw "QEMU framebuffer screenshot has invalid dimensions or pixel data ($width x $height, max $maximum)."
    }
    $colors = [Collections.Generic.HashSet[int]]::new()
    $pixelCount = $width * $height
    for ($pixel = 0; $pixel -lt $pixelCount; $pixel += 97) {
        $offset = $dataOffset + $pixel * 3
        $color = ([int]$bytes[$offset] -shl 16) -bor
                 ([int]$bytes[$offset + 1] -shl 8) -bor
                 [int]$bytes[$offset + 2]
        [void]$colors.Add($color)
    }
    if ($colors.Count -lt 8) {
        throw "QEMU framebuffer is blank or nearly uniform ($($colors.Count) sampled colors)."
    }
    Write-Output "  framebuffer: $width x $height, $($colors.Count) sampled colors"
}

$machine = 'q35'
if ($ProbeOnly) { $machine = 'q35,i8042=off' }
$xhciDevice = 'qemu-xhci,id=usb-controller'
if ($ProbeOnly -and $ProbeDevice -eq 'many') {
    $xhciDevice = 'qemu-xhci,id=usb-controller,p2=15,p3=15'
}
$arguments = @(
    '-machine', $machine, '-m', '512M', '-cpu', 'max', '-L', $shareDirectory,
    '-device', $xhciDevice,
    '-drive', "if=pflash,format=raw,unit=0,file=$firmwarePath,readonly=on",
    '-drive', "if=pflash,format=raw,unit=1,file=$varsCopyPath",
    '-drive', "format=raw,file=fat:rw:$imagePath",
    '-boot', 'order=c', '-display', 'none', '-serial', "file:$serialPath",
    '-monitor', "tcp:127.0.0.1:$monitorPort,server=on,wait=off"
)
if ($DiskSnapshotTest) {
    $diskSnapshotQemuPath = $diskSnapshotImagePath.Replace('\','/')
    $arguments += @('-drive', "if=none,id=bob64-snapshot-disk,format=raw,file=$diskSnapshotQemuPath",
        '-device', 'usb-storage,id=bob64-snapshot-storage,bus=usb-controller.0,port=1,drive=bob64-snapshot-disk')
}
if ($ProbeOnly) {
    if ($ProbeDevice -in @('storage','storage-hotplug','storage-active-disconnect','storage64')) {
        $storageImageQemuPath = $storageImagePath.Replace('\','/')
        if ($ProbeDevice -eq 'storage-active-disconnect') {
            $arguments += @('-drive', "if=none,id=usbdrive,format=raw,file=$storageImageQemuPath,bps=128")
        } else {
            $arguments += @('-drive', "if=none,id=usbdrive,format=raw,file=$storageImageQemuPath")
        }
        if ($ProbeDevice -eq 'storage-hotplug') {
            $storageReconnectImageQemuPath = $storageReconnectImagePath.Replace('\','/')
            $arguments += @('-drive', "if=none,id=usbdrive-reconnect,format=raw,file=$storageReconnectImageQemuPath")
        }
        if ($ProbeDevice -ne 'storage-active-disconnect') {
            $arguments += @('-device', 'usb-storage,id=probe-storage,bus=usb-controller.0,port=1,drive=usbdrive')
        }
    } elseif ($ProbeDevice -eq 'hub') {
        $arguments += @(
            '-device', 'usb-hub,id=probe-hub,bus=usb-controller.0,port=1',
            '-device', 'usb-kbd,id=probe-hub-keyboard,bus=usb-controller.0,port=1.1',
            '-device', 'usb-mouse,bus=usb-controller.0,port=1.2'
        )
    } else {
    $probeQemuDevices = if ($ProbeDevice -eq 'both') {
        @('usb-kbd','usb-mouse')
    } elseif ($ProbeDevice -eq 'many') {
        @('usb-kbd') + (@('usb-mouse') * 8)
    } elseif ($ProbeDevice -eq 'disconnect') {
        @('usb-kbd,id=probe-keyboard')
    } elseif ($ProbeDevice -eq 'mouse') { @('usb-mouse') } else { @('usb-kbd') }
    foreach ($probeQemuDevice in $probeQemuDevices) {
        $arguments += @('-device', "$probeQemuDevice,bus=usb-controller.0")
    }
    }
}

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = (Resolve-Path -LiteralPath $QemuPath).Path
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.Arguments = (($arguments | ForEach-Object {
    '"' + ([string]$_).Replace('"', '\"') + '"'
}) -join ' ')

$process = $null
try {
    $serialText = ''
    $process = [Diagnostics.Process]::Start($startInfo)
    if ($ProbeOnly) {
        $probeDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $inputMarker = if ($ProbeDevice -eq 'mouse') {
            'bob64 kernel: USB HID mouse input queued'
        } else { 'bob64 kernel: USB HID keyboard input queued' }
        $usbWheelMarker = 'bob64 kernel: USB HID mouse wheel input queued'
        $keyboardConfiguredMarker = 'bob64 kernel: USB HID boot keyboard endpoint configured; waiting for input report'
        $mouseConfiguredMarker = 'bob64 kernel: USB HID boot mouse endpoint configured; waiting for input report'
        $storageConfiguredMarker = 'bob64 kernel: USB mass-storage bulk endpoints configured slot='
        $storageCapacityMarker = 'bob64 kernel: USB MSC READ CAPACITY passed blocks=0000000000008000 sector-bytes=0000000000000200'
        $storageReadMarker = 'bob64 kernel: USB MSC sector-read passed lba=0 bytes=0000000000000200'
        $storage64Marker = 'bob64 kernel: USB MSC high-LBA READ(16) passed lba=0000000100000000'
        $storageWriteMarker = 'bob64 kernel: USB MSC sector-write/flush/readback passed lba=1 bytes=0000000000000200'
        $storageDisconnectedMarker = 'bob64 kernel: USB mass-storage device disconnected'
        $storageTransferArmedMarker = 'bob64 kernel: USB MSC READ(10) data transfer in flight'
        $storageTransferAbortedMarker = 'bob64 kernel: USB MSC transfer aborted after disconnect'
        $storageHotplugPhase = 0
        $storageActiveRemovalSent = $false
        $storageActiveAttached = $false
        $storageActiveAttachMarker = 'bob64: exiting UEFI boot services'
        $hubScannedMarker = 'bob64 kernel: USB 2 hub configured ports='
        $hubScanVerified = $false
        $hidConfiguredSeen = $false
        $testInputSent = $false
        $hidShellTested = $false
        $shellReadyMarker = 'bob64! kernel shell; type help'
        $keyboardShellMarker = 'bob64 kernel 0.1'
        $failureMarker = 'bob64 kernel: xHCI capability probe failed, result='
        while ([DateTime]::UtcNow -lt $probeDeadline) {
            if (Test-Path -LiteralPath $serialPath) {
                $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                if ($ProbeDevice -eq 'storage-active-disconnect' -and
                    -not $storageActiveAttached -and $serialText -and
                    $serialText.Contains($storageActiveAttachMarker)) {
                    $response=Send-MonitorCommand 'device_add usb-storage,id=probe-storage,bus=usb-controller.0,port=1,drive=usbdrive' -Capture
                    if ($response -match 'Error') {
                        throw "QEMU could not attach the throttled storage test device: $response"
                    }
                    $storageActiveAttached=$true
                }
                if ($ProbeDevice -eq 'storage-active-disconnect' -and
                    -not $storageActiveRemovalSent -and $serialText -and
                    $serialText.Contains($storageTransferArmedMarker)) {
                    $response=Send-MonitorCommand 'device_del probe-storage' -Capture
                    if ($response -match 'Error') {
                        throw "QEMU could not remove storage during the active transfer: $response"
                    }
                    $storageActiveRemovalSent=$true
                }
                if ($ProbeDevice -eq 'storage-active-disconnect' -and
                    $storageActiveRemovalSent -and $serialText -and
                    $serialText.Contains($storageTransferAbortedMarker) -and
                    $serialText.Contains($storageDisconnectedMarker)) {
                    Write-Output 'QEMU USB mass-storage removal during an outstanding xHCI transfer aborted the request and completed device teardown.'
                    return
                }
                $storageCapacityPassed = if ($ProbeDevice -eq 'storage64') {
                    $serialText -and $serialText.Contains($storage64Marker)
                } else { $serialText -and $serialText.Contains($storageCapacityMarker) }
                if ($ProbeDevice -in @('storage','storage-hotplug','storage64') -and $serialText -and
                    $serialText.Contains($storageConfiguredMarker) -and
                    $storageCapacityPassed -and
                    $serialText.Contains($storageReadMarker) -and
                    (-not $ExpectStorageWrite -or
                     $serialText.Contains($storageWriteMarker)) -and
                    $serialText.Contains($shellReadyMarker)) {
                    if ($ProbeDevice -eq 'storage') {
                        $storageStart = $serialText.IndexOf($storageConfiguredMarker)
                        Write-Output $serialText.Substring($storageStart,
                            [Math]::Min(500,$serialText.Length-$storageStart))
                        Write-Output 'QEMU USB mass-storage INQUIRY, READ CAPACITY, and READ(10) passed over xHCI bulk endpoints.'
                        if ($ExpectStorageWrite) {
                            Stop-QemuForStorageCheck
                            Assert-StorageTestPattern $storageImagePath
                            Write-Output 'QEMU USB mass-storage WRITE(10), SYNCHRONIZE CACHE, readback, bounds check, and post-shutdown persistence passed.'
                        }
                        return
                    }
                    if ($ProbeDevice -eq 'storage64') {
                        $storageStart = $serialText.IndexOf($storageConfiguredMarker)
                        Write-Output $serialText.Substring($storageStart,
                            [Math]::Min(700,$serialText.Length-$storageStart))
                        Write-Output 'QEMU USB mass storage passed READ CAPACITY(16) and read verified data above the 32-bit LBA boundary with READ(16).'
                        return
                    }
                    if ($storageHotplugPhase -eq 0) {
                        $response=Send-MonitorCommand 'device_del probe-storage' -Capture
                        if ($response -match 'Error') {
                            throw "QEMU could not remove the temporary USB storage device: $response"
                        }
                        $storageHotplugPhase=1
                    } elseif ($storageHotplugPhase -eq 1 -and
                              $serialText.Contains($storageDisconnectedMarker)) {
                        $response=Send-MonitorCommand 'device_add usb-storage,id=probe-storage-reconnected,bus=usb-controller.0,port=1,drive=usbdrive-reconnect' -Capture
                        if ($response -match 'Error') {
                            throw "QEMU could not re-add the temporary USB storage device: $response"
                        }
                        $storageHotplugPhase=2
                    } elseif ($storageHotplugPhase -eq 2 -and
                        [regex]::Matches($serialText,
                            [regex]::Escape($storageReadMarker)).Count -ge 2 -and
                        (-not $ExpectStorageWrite -or
                         [regex]::Matches($serialText,
                            [regex]::Escape($storageWriteMarker)).Count -ge 2)) {
                        Write-Output 'QEMU USB mass-storage INQUIRY, READ CAPACITY, and READ(10) passed before and after hotplug.'
                        if ($ExpectStorageWrite) {
                            Stop-QemuForStorageCheck
                            Assert-StorageTestPattern $storageImagePath
                            Assert-StorageTestPattern $storageReconnectImagePath
                            Write-Output 'QEMU USB mass-storage WRITE(10), SYNCHRONIZE CACHE, readback, and post-shutdown persistence passed before and after hotplug.'
                        }
                        return
                    }
                    if ($ProbeDevice -eq 'storage-hotplug') { continue }
                }
                if ($serialText -and $serialText.Contains($failureMarker)) {
                    Write-Output (Send-MonitorCommand 'info pci' -Capture)
                    Write-Output (Send-MonitorCommand 'xp /8wx 0xc000000000' -Capture)
                    throw "The xHCI MMIO capability probe failed.`n$serialText"
                }
                if ($ProbeDevice -eq 'hub' -and -not $hubScanVerified -and $serialText -and
                    $serialText.Contains($hubScannedMarker)) {
                    $hubScanLine = ($serialText -split "`r?`n" |
                        Where-Object { $_.Contains($hubScannedMarker) } |
                        Select-Object -Last 1)
                    if ($hubScanLine -notmatch 'configured ports=([0-9a-f]+) connected=([0-9a-f]+) reset=([0-9a-f]+)') {
                        throw "The USB hub scan marker was malformed.`n$hubScanLine"
                    }
                    $hubPortCount = [Convert]::ToInt32($Matches[1],16)
                    $hubConnectedCount = [Convert]::ToInt32($Matches[2],16)
                    $hubResetCount = [Convert]::ToInt32($Matches[3],16)
                    if ($hubPortCount -ne 8 -or $hubConnectedCount -lt 2 -or
                        $hubResetCount -lt 2) {
                        throw "The USB hub did not report both attached downstream devices.`n$hubScanLine"
                    }
                    Write-Output "QEMU USB 2 hub configured $hubPortCount ports, detected $hubConnectedCount children and reset $hubResetCount ports."
                    $hubScanVerified = $true
                }
                $allConfigured = $serialText -and
                    (($ProbeDevice -eq 'keyboard' -and $serialText.Contains($keyboardConfiguredMarker)) -or
                     ($ProbeDevice -eq 'mouse' -and $serialText.Contains($mouseConfiguredMarker)) -or
                     ($ProbeDevice -eq 'disconnect' -and $serialText.Contains($keyboardConfiguredMarker)) -or
                     ($ProbeDevice -eq 'both' -and $serialText.Contains($keyboardConfiguredMarker) -and
                      $serialText.Contains($mouseConfiguredMarker)) -or
                     ($ProbeDevice -eq 'many' -and
                      [regex]::Matches($serialText,[regex]::Escape($keyboardConfiguredMarker)).Count -ge 1 -and
                      [regex]::Matches($serialText,[regex]::Escape($mouseConfiguredMarker)).Count -ge 8) -or
                     ($ProbeDevice -eq 'hub' -and $hubScanVerified -and
                      $serialText.Contains($keyboardConfiguredMarker) -and
                      $serialText.Contains($mouseConfiguredMarker)))
                if ($allConfigured -and
                    -not $hidConfiguredSeen) {
                    Write-Output ($serialText.Substring(
                        $serialText.IndexOf('bob64 kernel: xHCI PCI controller found at'),
                        [Math]::Min(1600,$serialText.Length-
                            $serialText.IndexOf('bob64 kernel: xHCI PCI controller found at'))))
                    $hidConfiguredSeen = $true
                }
                if (-not $testInputSent -and $serialText -and
                    $serialText.Contains($shellReadyMarker)) {
                    if ($ProbeDevice -eq 'mouse') {
                        Send-MonitorCommand 'mouse_move 16 0'
                    } else {
                        Send-GuestText 'version'
                    }
                    $testInputSent = $true
                }
                if ($testInputSent -and -not $hidShellTested) {
                    $shellInputDeadline = [DateTime]::UtcNow.AddSeconds(8)
                    do {
                        Start-Sleep -Milliseconds 100
                        $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    } while ([DateTime]::UtcNow -lt $shellInputDeadline -and
                             (-not $serialText.Contains($inputMarker) -or
                              ($ProbeDevice -ne 'mouse' -and
                               -not $serialText.Contains($keyboardShellMarker))))
                    if (-not $serialText.Contains($inputMarker) -or
                        ($ProbeDevice -ne 'mouse' -and
                         -not $serialText.Contains($keyboardShellMarker))) {
                        throw "USB $ProbeDevice input did not reach the Bob input path.`n$serialText"
                    }
                    Write-Output "USB HID $ProbeDevice input reached Bob64 with i8042 disabled."
                    if ($ProbeDevice -eq 'hub') {
                        $hubChangeMarker = 'bob64 kernel: USB hub downstream change report received slot='
                        $hubAddResponse = Send-MonitorCommand 'device_add usb-kbd,id=hub-hotplug,bus=usb-controller.0,port=1.3' -Capture
                        if ($hubAddResponse -match '(?i)error|failed|not found') {
                            $hubTree = Send-MonitorCommand 'info qtree' -Capture
                            $hubBuses = ($hubTree -split "`r?`n" |
                                Where-Object { $_ -match '(?i)usb|hub' }) -join "`n"
                            throw "QEMU could not add the USB hub child: $hubAddResponse`n$hubBuses"
                        }
                        $hubChangeDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $hubChangeDeadline -and
                                 -not $serialText.Contains($hubChangeMarker))
                        if (-not $serialText.Contains($hubChangeMarker)) {
                            throw "The USB hub did not report the downstream hotplug change.`n$serialText"
                        }
                        $hubEnumerationMarker = 'bob64 kernel: USB hub hotplug device enumerated port=0000000000000003'
                        $hubEnumerationDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $hubEnumerationDeadline -and
                                 -not $serialText.Contains($hubEnumerationMarker))
                        if (-not $serialText.Contains($hubEnumerationMarker)) {
                            throw "The hub reported hotplug but bob64 did not enumerate the downstream device.`n$serialText"
                        }
                        Write-Output 'USB hub hotplug status was read and the downstream keyboard was enumerated.'
                        Send-MonitorCommand 'device_del probe-hub-keyboard'
                        $hubReleaseMarker = 'bob64 kernel: USB HID transfer stopped; held input released'
                        $hubInitialReleaseDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $hubInitialReleaseDeadline -and
                                 -not $serialText.Contains($hubReleaseMarker))
                        if (-not $serialText.Contains($hubReleaseMarker)) {
                            throw "Removing the original hub keyboard did not disconnect its HID child.`n$serialText"
                        }
                        $versionMarkerCount = [regex]::Matches($serialText,
                            [regex]::Escape($keyboardShellMarker)).Count
                        Send-GuestText 'version'
                        $hotplugInputDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                            $newVersionMarkerCount = [regex]::Matches($serialText,
                                [regex]::Escape($keyboardShellMarker)).Count
                        } while ([DateTime]::UtcNow -lt $hotplugInputDeadline -and
                                 $newVersionMarkerCount -le $versionMarkerCount)
                        if ($newVersionMarkerCount -le $versionMarkerCount) {
                            throw "The newly enumerated hub keyboard could not type into the shell.`n$serialText"
                        }
                        Write-Output 'USB hub hotplug keyboard input worked after removing the original keyboard.'
                        Send-MonitorCommand 'sendkey a 1000'
                        $heldKeyDeadline = [DateTime]::UtcNow.AddSeconds(5)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $heldKeyDeadline -and
                                 -not $serialText.Contains('bob64> a'))
                        if (-not $serialText.Contains('bob64> a')) {
                            throw "The hub-connected keyboard did not report the held key before removal.`n$serialText"
                        }
                        Send-MonitorCommand 'device_del hub-hotplug'
                        $hubHotplugReleaseDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $hubHotplugReleaseDeadline -and
                                 -not $serialText.Contains($hubReleaseMarker))
                        if (-not $serialText.Contains($hubReleaseMarker)) {
                            throw "Removing the hub-connected keyboard did not release its held input.`n$serialText"
                        }
                        Write-Output 'USB hub child removal released the held keyboard input.'
                        $hubReconnectMarker = 'bob64 kernel: USB hub hotplug device enumerated port=0000000000000001'
                        Send-MonitorCommand 'device_add usb-kbd,id=probe-hub-keyboard,bus=usb-controller.0,port=1.1'
                        $hubReconnectDeadline = [DateTime]::UtcNow.AddSeconds(12)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $hubReconnectDeadline -and
                                 -not $serialText.Contains($hubReconnectMarker))
                        if (-not $serialText.Contains($hubReconnectMarker)) {
                            throw "The hub keyboard did not re-enumerate after reconnect.`n$serialText"
                        }
                        $promptCountBeforeLineCleanup = [regex]::Matches($serialText,
                            [regex]::Escape($shellPromptMarker)).Count
                        Send-MonitorCommand 'sendkey backspace'
                        Send-MonitorCommand 'sendkey ret'
                        $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(5)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                            $promptCountAfterLineCleanup = [regex]::Matches($serialText,
                                [regex]::Escape($shellPromptMarker)).Count
                        } while ([DateTime]::UtcNow -lt $cleanupDeadline -and
                                 $promptCountAfterLineCleanup -le $promptCountBeforeLineCleanup)
                        if ($promptCountAfterLineCleanup -le $promptCountBeforeLineCleanup) {
                            throw "The shell input line did not recover after hub keyboard reconnection.`n$serialText"
                        }
                        Write-Output 'USB hub keyboard reconnected and the shell input line recovered.'
                    }
                    if ($ProbeDevice -eq 'disconnect') {
                        Send-MonitorCommand 'sendkey a 1000'
                        $heldKeyDeadline = [DateTime]::UtcNow.AddSeconds(3)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $heldKeyDeadline -and
                                 -not $serialText.Contains('bob64> a'))
                        if (-not $serialText.Contains('bob64> a')) {
                            throw "USB keyboard did not report the held key before disconnect.`n$serialText"
                        }
                        Send-MonitorCommand 'device_del probe-keyboard'
                        $releaseMarker = 'bob64 kernel: USB HID transfer stopped; held input released'
                        $releaseDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $releaseDeadline -and
                                 -not $serialText.Contains($releaseMarker))
                        if (-not $serialText.Contains($releaseMarker)) {
                            throw "Disconnecting the USB keyboard did not release its held input state.`n$serialText"
                        }
                        Write-Output 'USB keyboard hot-unplug released its held key state.'
                        $reconnectedMarker = 'bob64 kernel: USB HID device reconnected on root port '
                        Send-MonitorCommand 'device_add usb-kbd,id=probe-keyboard,bus=usb-controller.0'
                        $reconnectDeadline = [DateTime]::UtcNow.AddSeconds(12)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $reconnectDeadline -and
                                 -not $serialText.Contains($reconnectedMarker))
                        if (-not $serialText.Contains($reconnectedMarker)) {
                            throw "USB keyboard did not reconnect and enumerate after device_add.`n$serialText"
                        }
                        Send-MonitorCommand 'sendkey b'
                        Start-Sleep -Milliseconds 150
                        Send-MonitorCommand 'sendkey ret'
                        $inputDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        $reportMarker = 'bob64 kernel: USB HID transfer report received'
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $inputDeadline -and
                            [regex]::Matches($serialText,[regex]::Escape($reportMarker)).Count -lt 2)
                        if ([regex]::Matches($serialText,
                            [regex]::Escape($reportMarker)).Count -lt 2) {
                            throw "USB keyboard re-enumerated but its interrupt endpoint did not return a report.`n$serialText"
                        }
                        Write-Output 'USB keyboard hot-replug re-enumerated and returned an interrupt report.'
                        return
                    }
                    if ($ProbeDevice -eq 'both' -or $ProbeDevice -eq 'many' -or
                        $ProbeDevice -eq 'hub') {
                        Send-MonitorCommand 'mouse_move 16 0'
                        $mouseInputMarker = 'bob64 kernel: USB HID mouse input queued'
                        $mouseInputDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $mouseInputDeadline -and
                                 -not $serialText.Contains($mouseInputMarker))
                        if (-not $serialText.Contains($mouseInputMarker)) {
                            throw "The USB mouse did not deliver pointer input beside the USB keyboard.`n$serialText"
                        }
                        $shellMarker = 'bob64> '
                        $shellCountBeforeMouseApp = [regex]::Matches($serialText,
                            [regex]::Escape($shellMarker)).Count
                        Send-GuestText 'run mousesmoke.b64e'
                        $mouseAppStartDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $mouseAppStartDeadline -and
                                 -not $serialText.Contains($mouseMarker))
                        if (-not $serialText.Contains($mouseMarker)) {
                            throw "USB mouse movement was not routed into the managed window.`n$serialText"
                        }
                        Send-MonitorCommand 'mouse_move 0 0 -1'
                        $mouseAppWheelDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $mouseAppWheelDeadline -and
                                 -not $serialText.Contains($mouseWheelMarker))
                        if (-not $serialText.Contains($mouseWheelMarker)) {
                            throw "USB mouse wheel was not routed into the managed window.`n$serialText"
                        }
                        Send-MonitorCommand 'mouse_move 0 10'
                        Start-Sleep -Milliseconds 100
                        Send-MonitorCommand 'mouse_button 1'
                        Start-Sleep -Milliseconds 100
                        Send-MonitorCommand 'mouse_button 0'
                        $mouseAppButtonDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $mouseAppButtonDeadline -and
                                 -not $serialText.Contains($mouseButtonMarker))
                        if (-not $serialText.Contains($mouseButtonMarker)) {
                            throw "USB mouse button input was not routed into the managed window.`n$serialText"
                        }
                        $mouseExitDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $mouseExitDeadline -and
                                 [regex]::Matches($serialText,
                                    [regex]::Escape($shellMarker)).Count -le $shellCountBeforeMouseApp)
                        if ([regex]::Matches($serialText,
                            [regex]::Escape($shellMarker)).Count -le $shellCountBeforeMouseApp) {
                            throw "The managed mouse smoke app did not return to the shell.`n$serialText"
                        }
                        $bobCountBeforeDesktop = [regex]::Matches($serialText,
                            [regex]::Escape('bob!')).Count
                        $shellCountBeforeDesktop = [regex]::Matches($serialText,
                            [regex]::Escape($shellMarker)).Count
                        Send-GuestText 'run desktop.b64e'
                        $desktopStartDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $desktopStartDeadline -and
                            [regex]::Matches($serialText,[regex]::Escape('bob!')).Count -le
                                $bobCountBeforeDesktop)
                        if ([regex]::Matches($serialText,
                            [regex]::Escape('bob!')).Count -le $bobCountBeforeDesktop) {
                            throw "The USB keyboard could not launch the Bob desktop.`n$serialText"
                        }
                        Start-Sleep -Milliseconds 500
                        Send-MonitorCommand 'sendkey tab'
                        Start-Sleep -Milliseconds 120
                        Send-MonitorCommand 'sendkey h'
                        Start-Sleep -Milliseconds 120
                        Send-MonitorCommand 'sendkey esc'
                        Start-Sleep -Milliseconds 120
                        Send-MonitorCommand 'sendkey esc'
                        $desktopExitDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $desktopExitDeadline -and
                            [regex]::Matches($serialText,
                                [regex]::Escape($shellMarker)).Count -le $shellCountBeforeDesktop)
                        if ([regex]::Matches($serialText,
                            [regex]::Escape($shellMarker)).Count -le $shellCountBeforeDesktop) {
                            throw "The USB keyboard could not close desktop Help and return to the shell.`n$serialText"
                        }
                        Write-Output 'USB keyboard launched the shell app; USB mouse move and wheel reached its managed window.'
                        Write-Output 'USB keyboard launched the desktop, opened and closed Help, and returned to the shell.'
                    }
                    if ($ProbeDevice -eq 'mouse') {
                        Send-MonitorCommand 'mouse_move 0 0 -1'
                        $wheelDeadline = [DateTime]::UtcNow.AddSeconds(8)
                        do {
                            Start-Sleep -Milliseconds 100
                            $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        } while ([DateTime]::UtcNow -lt $wheelDeadline -and
                                 -not $serialText.Contains($usbWheelMarker))
                        if (-not $serialText.Contains($usbWheelMarker)) {
                            throw "USB mouse wheel input did not reach Bob64.`n$serialText"
                        }
                        Write-Output 'USB HID mouse wheel input reached Bob64 with i8042 disabled.'
                    }
                    if (-not $hidConfiguredSeen) {
                        throw "USB $ProbeDevice input worked, but the expected HID devices were not all configured.`n$serialText"
                    }
                    $hidShellTested = $true
                    if ($ProbeDevice -eq 'many') {
                        Write-Output 'Nine directly connected USB HID devices configured with independent xHCI slots and rings.'
                    }
                    return
                }
            }
            if ($process.HasExited) {
            throw "QEMU exited before the USB $ProbeDevice probe completed.`n$serialText"
            }
            Start-Sleep -Milliseconds 100
        }
        throw "Timed out waiting for the USB $ProbeDevice probe.`n$serialText"
    }
    $overallTimeout = [Math]::Max($TimeoutSeconds,180)
    $deadline = [DateTime]::UtcNow.AddSeconds($overallTimeout)
    $serialText = ''
    $mouseSmokeStarted = $false
    $graphicsSmokeStarted = $false
    $nativeLauncherSmokeStarted = $false
    $compilerSmokeStarted = $false
    $launcherSmokeStarted = $false
    $restoredCompiledAppChecked = $false
    $restoredDesktopChecked = $false
    $snapshotSaved = $false
    $persistentMarkerCountBeforeReset = 0
    $shellMarkerCountBeforeReset = 0
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
            if ($allPresent -and -not $mouseSmokeStarted) {
                $shellMarkerCountBeforeMouse = [regex]::Matches($serialText,
                    [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeMouse = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                Send-MonitorCommand 'mouse_move 32 0'
                Send-GuestText 'run mousesmoke.b64e'
                $mouseDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $mouseDeadline -and
                         -not $serialText.Contains($mouseMarker))
                if (-not $serialText.Contains($mouseMarker)) {
                    throw "The live mouse IRQ smoke app did not receive a mouse-move event.`n$serialText"
                }
                Send-MonitorCommand 'mouse_move 0 0 -1'
                $mouseWheelDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $mouseWheelDeadline -and
                         -not $serialText.Contains($mouseWheelMarker))
                if (-not $serialText.Contains($mouseWheelMarker)) {
                    throw "The live mouse IRQ smoke app did not receive a wheel event.`n$serialText"
                }
                Send-MonitorCommand 'mouse_move 0 10'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                $mouseButtonDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $mouseButtonDeadline -and
                         -not $serialText.Contains($mouseButtonMarker))
                if (-not $serialText.Contains($mouseButtonMarker)) {
                    throw "The live mouse app did not activate its managed-window button.`n$serialText"
                }
                Send-MonitorCommand 'mouse_move 0 -10'
                $mouseExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $mouseExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeMouse -or
                          $exitMarkerCount -le $exitMarkerCountBeforeMouse))
                if ($shellMarkerCount -le $shellMarkerCountBeforeMouse -or
                    $exitMarkerCount -le $exitMarkerCountBeforeMouse) {
                    throw "The mouse window app did not exit cleanly to the shell.`n$serialText"
                }
                $mouseSmokeStarted = $true
            }
            if ($allPresent -and $mouseSmokeStarted -and -not $graphicsSmokeStarted) {
                $bobMarkerCountBeforeGraphics =
                    [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                $shellMarkerCountBeforeGraphics =
                    [regex]::Matches($serialText, [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeGraphics = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                Send-GuestText 'run display.b64e'
                $graphicsDeadline = [DateTime]::UtcNow.AddSeconds(15)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $graphicsDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeGraphics)
                if ($bobMarkerCount -le $bobMarkerCountBeforeGraphics) {
                    throw "The native graphics demo did not present its surface and print bob!.`n$serialText"
                }
                # mousesmoke leaves the pointer at (672, 400). Drag the
                # centered window by (80, 40) using a title-bar point away
                # from the app's Close button, then click Close at its moved
                # screen position (988, 259).
                Send-MonitorCommand 'mouse_move -16 -95'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move -16 -95'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_move 80 40'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Send-MonitorCommand 'mouse_move 90 3'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move 90 3'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move 88 3'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                $graphicsExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $graphicsExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeGraphics -or
                          $exitMarkerCount -le $exitMarkerCountBeforeGraphics))
                if ($shellMarkerCount -le $shellMarkerCountBeforeGraphics -or
                    $exitMarkerCount -le $exitMarkerCountBeforeGraphics) {
                    throw "The dragged native graphics window did not close back to the shell.`n$serialText"
                }
                # Return the pointer to (672, 400) for later mouse tests.
                Send-MonitorCommand 'mouse_move -106 47'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move -105 47'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move -105 47'
                $graphicsSmokeStarted = $true
            }
            if ($allPresent -and $graphicsSmokeStarted -and
                -not $nativeLauncherSmokeStarted) {
                $bobMarkerCountBeforeNativeLauncher =
                    [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                $shellMarkerCountBeforeNativeLauncher =
                    [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeNativeLauncher = [regex]::Matches(
                    $serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                Send-GuestText 'run launcher.b64e'
                $nativeLauncherDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $nativeLauncherDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeNativeLauncher)
                if ($bobMarkerCount -le $bobMarkerCountBeforeNativeLauncher) {
                    throw "The native windowed launcher did not print bob!.`n$serialText"
                }
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey ret'
                $nativeLauncherChildDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $nativeLauncherChildDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeNativeLauncher+1)
                if ($bobMarkerCount -le $bobMarkerCountBeforeNativeLauncher+1) {
                    throw "The native windowed launcher did not run its selected bob! app.`n$serialText"
                }
                # Escape closes the child and returns to the launcher. A
                # press on Close released outside must cancel; a full click
                # inside the button must then close the launcher.
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey esc'
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'mouse_move -42 -47'
                Send-MonitorCommand 'mouse_move -42 -47'
                Send-MonitorCommand 'mouse_button 1'
                Send-MonitorCommand 'mouse_move 65 50'
                Send-MonitorCommand 'mouse_button 0'
                Start-Sleep -Milliseconds 300
                $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                $shellMarkerCount = [regex]::Matches($serialText,
                    [regex]::Escape($shellPromptMarker)).Count
                if ($shellMarkerCount -gt $shellMarkerCountBeforeNativeLauncher) {
                    throw "The launcher closed after a press was released outside its Close button.`n$serialText"
                }
                Send-MonitorCommand 'mouse_move -65 -50'
                Send-MonitorCommand 'mouse_button 1'
                Send-MonitorCommand 'mouse_button 0'
                Send-MonitorCommand 'mouse_move 42 47'
                Send-MonitorCommand 'mouse_move 42 47'
                $nativeLauncherExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $nativeLauncherExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeNativeLauncher -or
                          $exitMarkerCount -le $exitMarkerCountBeforeNativeLauncher))
                if ($shellMarkerCount -le $shellMarkerCountBeforeNativeLauncher -or
                    $exitMarkerCount -le $exitMarkerCountBeforeNativeLauncher) {
                    throw "The native windowed launcher did not close after a complete mouse click inside Close.`n$serialText"
                }
                $bobMarkerCountBeforeForwardedArgs = $bobMarkerCount
                $shellMarkerCountBeforeForwardedArgs = $shellMarkerCount
                $exitMarkerCountBeforeForwardedArgs = $exitMarkerCount
                Send-GuestText 'run launcher.b64e echo.b64e argumentforwarded'
                $forwardedArgsDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                    $forwardedArgCount = [regex]::Matches($serialText,
                        [regex]::Escape('argumentforwarded')).Count
                } while ([DateTime]::UtcNow -lt $forwardedArgsDeadline -and
                         ($bobMarkerCount -le $bobMarkerCountBeforeForwardedArgs -or
                          $forwardedArgCount -lt 2 -or
                          $shellMarkerCount -le $shellMarkerCountBeforeForwardedArgs -or
                          $exitMarkerCount -le $exitMarkerCountBeforeForwardedArgs))
                if ($bobMarkerCount -le $bobMarkerCountBeforeForwardedArgs -or
                    $forwardedArgCount -lt 2 -or
                    $shellMarkerCount -le $shellMarkerCountBeforeForwardedArgs -or
                    $exitMarkerCount -le $exitMarkerCountBeforeForwardedArgs) {
                    throw "The launcher did not forward argv to echo.b64e and return to the shell.`n$serialText"
                }
                $bobMarkerCountBeforeGuiLs = $bobMarkerCount
                $shellMarkerCountBeforeGuiLs = $shellMarkerCount
                $exitMarkerCountBeforeGuiLs = $exitMarkerCount
                Send-GuestText 'run ls.b64e gui bob.c'
                $guiLsDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $guiLsDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeGuiLs)
                if ($bobMarkerCount -le $bobMarkerCountBeforeGuiLs) {
                    throw "ls.b64e gui did not open its windowed file list.`n$serialText"
                }
                $bobMarkerCountBeforeGuiLsOpen = $bobMarkerCount
                Start-Sleep -Milliseconds 300
                # bob.c is preselected at row 9 in the current RAM listing.
                # On the 1280x800 display, the centered Files window puts that
                # row near (440, 442).
                # The pointer starts at (672, 400); double-click the row.
                Send-MonitorCommand 'mouse_move -77 14'
                Send-MonitorCommand 'mouse_move -77 14'
                Send-MonitorCommand 'mouse_move -78 14'
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                $guiLsOpenDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $guiLsOpenDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeGuiLsOpen)
                if ($bobMarkerCount -le $bobMarkerCountBeforeGuiLsOpen) {
                    throw "A double-click in ls.b64e gui did not open bob.c in cat's windowed viewer.`n$serialText"
                }
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey esc'
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey esc'
                # Restore the pointer position expected by subsequent tests.
                Send-MonitorCommand 'mouse_move 77 -14'
                Send-MonitorCommand 'mouse_move 77 -14'
                Send-MonitorCommand 'mouse_move 78 -14'
                $guiLsExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $guiLsExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeGuiLs -or
                          $exitMarkerCount -le $exitMarkerCountBeforeGuiLs))
                if ($shellMarkerCount -le $shellMarkerCountBeforeGuiLs -or
                    $exitMarkerCount -le $exitMarkerCountBeforeGuiLs) {
                    throw "The windowed ls app did not close back to the shell on Escape.`n$serialText"
                }
                $bobMarkerCountBeforeGuiCat = [regex]::Matches($serialText,
                    [regex]::Escape('bob!')).Count
                $shellMarkerCountBeforeGuiCat = $shellMarkerCount
                $exitMarkerCountBeforeGuiCat = $exitMarkerCount
                Send-GuestText 'run cat.b64e gui bob.c'
                $guiCatDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $guiCatDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeGuiCat)
                if ($bobMarkerCount -le $bobMarkerCountBeforeGuiCat) {
                    throw "cat.b64e gui did not open the requested text file in a window.`n$serialText"
                }
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey down'
                Send-MonitorCommand 'sendkey pgdn'
                Send-MonitorCommand 'sendkey esc'
                $guiCatExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $guiCatExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeGuiCat -or
                          $exitMarkerCount -le $exitMarkerCountBeforeGuiCat))
                if ($shellMarkerCount -le $shellMarkerCountBeforeGuiCat -or
                    $exitMarkerCount -le $exitMarkerCountBeforeGuiCat) {
                    throw "The text viewer did not return to the shell on Escape.`n$serialText"
                }
                $bobMarkerCountBeforeGuiInfo = [regex]::Matches($serialText,
                    [regex]::Escape('bob!')).Count
                $shellMarkerCountBeforeGuiInfo = $shellMarkerCount
                $exitMarkerCountBeforeGuiInfo = $exitMarkerCount
                Send-GuestText 'run info.b64e gui'
                $guiInfoDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $guiInfoDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeGuiInfo)
                if ($bobMarkerCount -le $bobMarkerCountBeforeGuiInfo) {
                    throw "info.b64e gui did not open its managed system-information window.`n$serialText"
                }
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey r'
                Send-MonitorCommand 'sendkey esc'
                $guiInfoExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $guiInfoExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeGuiInfo -or
                          $exitMarkerCount -le $exitMarkerCountBeforeGuiInfo))
                if ($shellMarkerCount -le $shellMarkerCountBeforeGuiInfo -or
                    $exitMarkerCount -le $exitMarkerCountBeforeGuiInfo) {
                    throw "The system-information window did not close back to the shell.`n$serialText"
                }
                $bobMarkerCountBeforeGuiEcho = [regex]::Matches($serialText,
                    [regex]::Escape('bob!')).Count
                $shellMarkerCountBeforeGuiEcho = $shellMarkerCount
                $exitMarkerCountBeforeGuiEcho = $exitMarkerCount
                $echoGuiArguments = ('argument123 ' * 18).TrimEnd()
                Send-GuestText "run echo.b64e gui $echoGuiArguments"
                $guiEchoDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $guiEchoDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeGuiEcho)
                if ($bobMarkerCount -le $bobMarkerCountBeforeGuiEcho) {
                    throw "echo.b64e gui did not create its managed argument window.`n$serialText"
                }
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey down'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'sendkey pgdn'
                Start-Sleep -Milliseconds 100
                # The pointer is at (672, 400); click the echo window's Close
                # control at (908, 213) after its wrapped text is presented.
                Send-MonitorCommand 'mouse_move 79 -62'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move 79 -62'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move 78 -62'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                # Restore the pointer expected by later runtime interactions.
                Send-MonitorCommand 'mouse_move -79 62'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move -79 62'
                Start-Sleep -Milliseconds 80
                Send-MonitorCommand 'mouse_move -78 62'
                $guiEchoExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $guiEchoExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeGuiEcho -or
                          $exitMarkerCount -le $exitMarkerCountBeforeGuiEcho))
                if ($shellMarkerCount -le $shellMarkerCountBeforeGuiEcho -or
                    $exitMarkerCount -le $exitMarkerCountBeforeGuiEcho) {
                    throw "The echo argument window did not handle its input and return to the shell.`n$serialText"
                }
                Write-Output '  echo GUI: wrapped managed argv text, handled scroll keys, and closed through the mouse button'
                $nativeLauncherSmokeStarted = $true
            }
            if ($allPresent -and $graphicsSmokeStarted -and -not $compilerSmokeStarted) {
                Send-GuestText 'cc bob.c compiled.b64e'
                $compileDeadline = [DateTime]::UtcNow.AddSeconds(15)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $compileDeadline -and
                         -not $serialText.Contains('compiled to compiled.b64e'))
                if (-not $serialText.Contains('compiled to compiled.b64e')) {
                    throw "The resident C compiler did not produce a B64E app.`n$serialText"
                }
                $bobMarkerCountBeforeRun =
                    [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                Send-GuestText 'run compiled.b64e'
                $runDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $runDeadline -and
                         (-not $serialText.Contains('application exit status=0x0000000000000000') -or
                          $bobMarkerCount -le $bobMarkerCountBeforeRun))
                if (-not $serialText.Contains('application exit status=0x0000000000000000') -or
                    $bobMarkerCount -le $bobMarkerCountBeforeRun) {
                    throw "The compiled C application did not print bob! and exit successfully.`n$serialText"
                }
                $compilerSmokeStarted = $true
            }
            if ($allPresent -and $compilerSmokeStarted -and -not $launcherSmokeStarted) {
                $bobMarkerCountBeforeDesktop =
                    [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                $shellMarkerCountBeforeDesktop =
                    [regex]::Matches($serialText, [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeDesktop =
                    [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                Send-GuestText 'run desktop.b64e'
                $desktopDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $desktopDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeDesktop)
                if ($bobMarkerCount -le $bobMarkerCountBeforeDesktop) {
                    throw "The native desktop did not start for the launcher smoke test.`n$serialText"
                }
                Start-Sleep -Milliseconds 500
                Assert-FramebufferRendered $framebufferPath
                # Tab focuses the Files window. Its first row is bob.b64e;
                # Enter should run it and restore the still-open desktop.
                $bobMarkerCountBeforeEnterRun = [regex]::Matches($serialText,
                    [regex]::Escape('bob!')).Count
                Send-MonitorCommand 'sendkey tab'
                Start-Sleep -Milliseconds 150
                Send-MonitorCommand 'sendkey ret'
                $enterRunDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $enterRunDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeEnterRun)
                if ($bobMarkerCount -le $bobMarkerCountBeforeEnterRun) {
                    throw "Enter did not run the selected bob.b64e app from the desktop file browser.`n$serialText"
                }
                Start-Sleep -Milliseconds 300
                # The PS/2 pointer is at (672, 400). Click the editor's name
                # field at (512, 60), replace its filename, and save as.
                Send-MonitorCommand 'mouse_move -53 -113'
                Start-Sleep -Milliseconds 50
                Send-MonitorCommand 'mouse_move -53 -113'
                Start-Sleep -Milliseconds 50
                Send-MonitorCommand 'mouse_move -54 -114'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Send-MonitorCommand 'sendkey home'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'sendkey right'
                Send-MonitorCommand 'sendkey right'
                Send-MonitorCommand 'sendkey x'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'sendkey backspace'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'sendkey home'
                Send-MonitorCommand 'sendkey right'
                Send-MonitorCommand 'sendkey delete'
                Send-MonitorCommand 'sendkey o'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'sendkey end'
                Start-Sleep -Milliseconds 350
                foreach ($index in 1..9) {
                    Send-MonitorCommand 'sendkey backspace'
                    Start-Sleep -Milliseconds 350
                }
                Send-GuestText 'saved.txt' -delayMilliseconds 350
                Send-MonitorCommand 'sendkey ctrl-s'
                Start-Sleep -Milliseconds 500
                # The pointer is on the filename field at (512, 60). Click
                # APPS, run the selected app, then close the launcher by mouse.
                foreach ($step in 1..4) {
                    Send-MonitorCommand 'mouse_move -97 0'
                    Start-Sleep -Milliseconds 50
                }
                Send-MonitorCommand 'mouse_move -2 10'
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Start-Sleep -Milliseconds 250
                # Launcher Run App is centered at (1006, 78).
                foreach ($step in 1..9) {
                    Send-MonitorCommand 'mouse_move 98 0'
                    Start-Sleep -Milliseconds 50
                }
                Send-MonitorCommand 'mouse_move 2 8'
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                $launcherDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $launcherDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeDesktop+1)
                if ($bobMarkerCount -le $bobMarkerCountBeforeDesktop+1) {
                    throw "The desktop Applications launcher did not run its selected bob! app.`n$serialText"
                }
                Start-Sleep -Milliseconds 500
                # Close the Applications window through its X CLOSE button.
                Send-MonitorCommand 'mouse_move 72 0'
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Start-Sleep -Milliseconds 200
                Send-MonitorCommand 'sendkey esc'
                $desktopExitDeadline = [DateTime]::UtcNow.AddSeconds(2)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $desktopExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeDesktop -or
                          $exitMarkerCount -le $exitMarkerCountBeforeDesktop))
                if ($shellMarkerCount -le $shellMarkerCountBeforeDesktop -or
                    $exitMarkerCount -le $exitMarkerCountBeforeDesktop) {
                    Send-MonitorCommand 'sendkey esc'
                    $desktopExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                    do {
                        Start-Sleep -Milliseconds 100
                        $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                        $shellMarkerCount =
                            [regex]::Matches($serialText, [regex]::Escape($shellPromptMarker)).Count
                        $exitMarkerCount = [regex]::Matches($serialText,
                            [regex]::Escape('application exit status=0x0000000000000000')).Count
                    } while ([DateTime]::UtcNow -lt $desktopExitDeadline -and
                             ($shellMarkerCount -le $shellMarkerCountBeforeDesktop -or
                              $exitMarkerCount -le $exitMarkerCountBeforeDesktop))
                }
                if ($shellMarkerCount -le $shellMarkerCountBeforeDesktop -or
                    $exitMarkerCount -le $exitMarkerCountBeforeDesktop) {
                    throw "The desktop did not return successfully to the shell after its child app ran.`n$serialText"
                }
                Send-GuestText 'ls'
                $saveAsDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $saveAsDeadline -and
                         -not $serialText.Contains('saved.txt'))
                if (-not $serialText.Contains('saved.txt')) {
                    throw "The desktop filename field did not save the document as saved.txt.`n$serialText"
                }
                Send-GuestText 'rm saved.txt'
                $saveAsCleanupDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $saveAsCleanupDeadline -and
                         -not $serialText.Contains('deleted'))
                if (-not $serialText.Contains('deleted')) {
                    throw "The Save As QEMU fixture could not be removed.`n$serialText"
                }
                $guiDeleteFixture = 'guideletefixture.txt'
                Send-GuestText "write $guiDeleteFixture survivescancel"
                Start-Sleep -Milliseconds 200
                Send-GuestText "cat $guiDeleteFixture"
                $guiDeleteFixtureDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $guiDeleteFixtureDeadline -and
                         -not $serialText.Contains('survivescancel'))
                if (-not $serialText.Contains('survivescancel')) {
                    throw "The GUI delete test fixture could not be created and read back.`n$serialText"
                }
                $shellMarkerCountBeforeGuiCancel = [regex]::Matches($serialText,
                    [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeGuiCancel = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                Send-GuestText 'run desktop.b64e'
                $guiCancelStartDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $guiCancelStartDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeDesktop+2)
                if ($bobMarkerCount -le $bobMarkerCountBeforeDesktop+2) {
                    throw "The desktop did not reopen for Help and delete-dialog tests.`n$serialText"
                }
                Start-Sleep -Milliseconds 300
                # Focus Files, test popup dismissal, close Help, then click
                # Cancel in the delete dialog for the fixture.
                Send-MonitorCommand 'sendkey tab'
                Start-Sleep -Milliseconds 120
                Send-MonitorCommand 'sendkey m'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'sendkey down'
                Start-Sleep -Milliseconds 100
                # The pointer is at (1078, 78). Click outside the popup at
                # (200, 400) to dismiss it after keyboard navigation.
                foreach ($step in 1..8) {
                    Send-MonitorCommand 'mouse_move -100 40'
                    Start-Sleep -Milliseconds 60
                }
                Send-MonitorCommand 'mouse_move -78 2'
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Start-Sleep -Milliseconds 100
                # Reopen the popup and activate Close Menu with a mouse
                # press/release inside its fourth row.
                Send-MonitorCommand 'sendkey m'
                Start-Sleep -Milliseconds 150
                # From (200, 400), the fourth row is centered at (30, 157).
                Send-MonitorCommand 'mouse_move -170 -243'
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Start-Sleep -Milliseconds 150
                Send-MonitorCommand 'sendkey h'
                Start-Sleep -Milliseconds 350
                Send-MonitorCommand 'sendkey h'
                Start-Sleep -Milliseconds 350
                Send-MonitorCommand 'sendkey d'
                Start-Sleep -Milliseconds 500
                # The pointer is at (30, 157); Cancel is centered at (684, 431).
                Send-MonitorCommand 'mouse_move 654 274'
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Start-Sleep -Milliseconds 150
                Send-MonitorCommand 'sendkey esc'
                $guiCancelExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $guiCancelExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeGuiCancel -or
                          $exitMarkerCount -le $exitMarkerCountBeforeGuiCancel))
                if ($shellMarkerCount -le $shellMarkerCountBeforeGuiCancel -or
                    $exitMarkerCount -le $exitMarkerCountBeforeGuiCancel) {
                    throw "Help dismissal or delete cancellation left the desktop in an unexpected state.`n$serialText"
                }
                $fixtureReadCountBeforeCancelCheck = [regex]::Matches($serialText,
                    [regex]::Escape('survivescancel')).Count
                Send-GuestText "cat $guiDeleteFixture"
                $guiCancelCheckDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $fixtureReadCount = [regex]::Matches($serialText,
                        [regex]::Escape('survivescancel')).Count
                } while ([DateTime]::UtcNow -lt $guiCancelCheckDeadline -and
                         $fixtureReadCount -le $fixtureReadCountBeforeCancelCheck)
                if ($fixtureReadCount -le $fixtureReadCountBeforeCancelCheck) {
                    throw "Cancelling the GUI delete dialog did not preserve its file.`n$serialText"
                }
                $shellMarkerCountBeforeGuiDelete = [regex]::Matches($serialText,
                    [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeGuiDelete = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                Send-GuestText 'run desktop.b64e'
                $guiDeleteStartDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $guiDeleteStartDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeDesktop+3)
                if ($bobMarkerCount -le $bobMarkerCountBeforeDesktop+3) {
                    throw "The desktop did not reopen for the confirmed-delete test.`n$serialText"
                }
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey tab'
                Start-Sleep -Milliseconds 120
                Send-MonitorCommand 'sendkey d'
                Start-Sleep -Milliseconds 500
                Send-MonitorCommand 'sendkey y'
                Start-Sleep -Milliseconds 150
                Send-MonitorCommand 'sendkey esc'
                $guiDeleteExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $guiDeleteExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeGuiDelete -or
                          $exitMarkerCount -le $exitMarkerCountBeforeGuiDelete))
                if ($shellMarkerCount -le $shellMarkerCountBeforeGuiDelete -or
                    $exitMarkerCount -le $exitMarkerCountBeforeGuiDelete) {
                    throw "The desktop did not return after confirming the GUI delete dialog.`n$serialText"
                }
                $fileNotFoundCountBeforeDeleteCheck = [regex]::Matches($serialText,
                    [regex]::Escape('file not found')).Count
                Send-GuestText "cat $guiDeleteFixture"
                $guiDeleteCheckDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $fileNotFoundCount = [regex]::Matches($serialText,
                        [regex]::Escape('file not found')).Count
                } while ([DateTime]::UtcNow -lt $guiDeleteCheckDeadline -and
                         $fileNotFoundCount -le $fileNotFoundCountBeforeDeleteCheck)
                if ($fileNotFoundCount -le $fileNotFoundCountBeforeDeleteCheck) {
                    throw "Confirming the GUI delete dialog did not remove its selected fixture.`n$serialText"
                }
                Write-Output '  desktop popup outside dismissal and mouse-selected Close Menu, delete Cancel mouse click, and Yes keyboard confirmation passed'
                $guiUnsavedFixture = 'guiunsavedfixture.txt'
                Send-GuestText "write $guiUnsavedFixture preserved"
                $unsavedCreateDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $unsavedCreateDeadline -and
                         -not $serialText.Contains("write $guiUnsavedFixture preserved"))
                if (-not $serialText.Contains("write $guiUnsavedFixture preserved")) {
                    throw "The unsaved-edit test fixture could not be created.`n$serialText"
                }
                $shellMarkerCountBeforeUnsavedEditor = [regex]::Matches($serialText,
                    [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeUnsavedEditor = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                $bobMarkerCountBeforeUnsavedEditor = [regex]::Matches($serialText,
                    [regex]::Escape('bob!')).Count
                Send-GuestText "run desktop.b64e $guiUnsavedFixture"
                $unsavedEditorDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $unsavedEditorDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeUnsavedEditor)
                if ($bobMarkerCount -le $bobMarkerCountBeforeUnsavedEditor) {
                    throw "The desktop could not open the unsaved-edit fixture.`n$serialText"
                }
                Send-MonitorCommand 'sendkey end'
                Send-MonitorCommand 'sendkey x'
                Start-Sleep -Milliseconds 150
                Send-MonitorCommand 'sendkey esc'
                Start-Sleep -Milliseconds 250
                $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                $shellMarkerCount = [regex]::Matches($serialText,
                    [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCount = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                if ($shellMarkerCount -gt $shellMarkerCountBeforeUnsavedEditor -or
                    $exitMarkerCount -gt $exitMarkerCountBeforeUnsavedEditor) {
                    throw "One Escape discarded a dirty editor and closed the desktop without warning.`n$serialText"
                }
                Send-MonitorCommand 'sendkey down'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'sendkey esc'
                Start-Sleep -Milliseconds 250
                $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                $shellMarkerCount = [regex]::Matches($serialText,
                    [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCount = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                if ($shellMarkerCount -gt $shellMarkerCountBeforeUnsavedEditor -or
                    $exitMarkerCount -gt $exitMarkerCountBeforeUnsavedEditor) {
                    throw "A non-Escape key failed to cancel the dirty-editor close warning.`n$serialText"
                }
                Send-MonitorCommand 'sendkey esc'
                Start-Sleep -Milliseconds 120
                Send-MonitorCommand 'sendkey esc'
                $unsavedExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $unsavedExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeUnsavedEditor -or
                          $exitMarkerCount -le $exitMarkerCountBeforeUnsavedEditor))
                if ($shellMarkerCount -le $shellMarkerCountBeforeUnsavedEditor -or
                    $exitMarkerCount -le $exitMarkerCountBeforeUnsavedEditor) {
                    throw "The second Escape did not close the dirty editor and return to the shell.`n$serialText"
                }
                Send-GuestText "cat $guiUnsavedFixture"
                $unsavedVerifyDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $unsavedVerifyDeadline -and
                         $serialText -notmatch '(?m)^preserved\r?$')
                if ($serialText -notmatch '(?m)^preserved\r?$') {
                    throw "Closing the dirty editor changed or lost the saved file contents.`n$serialText"
                }
                Send-GuestText "rm $guiUnsavedFixture"
                $unsavedCleanupDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $unsavedCleanupDeadline -and
                         -not $serialText.Contains('deleted'))
                if (-not $serialText.Contains('deleted')) {
                    throw "The unsaved-editor fixture could not be cleaned up.`n$serialText"
                }
                Write-Output '  dirty editor: one Escape warns, other keys cancel, second Escape exits without saving'
                $guiEditFixture = 'guieditfixture.txt'
                Send-GuestText "write $guiEditFixture editable"
                $editCreateDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $editCreateDeadline -and
                         -not $serialText.Contains("write $guiEditFixture editable"))
                if (-not $serialText.Contains("write $guiEditFixture editable")) {
                    throw "The editor-save test fixture could not be created.`n$serialText"
                }
                $shellMarkerCountBeforeEditorSave = [regex]::Matches($serialText,
                    [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeEditorSave = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                $bobMarkerCountBeforeEditorSave = [regex]::Matches($serialText,
                    [regex]::Escape('bob!')).Count
                Send-GuestText "run desktop.b64e $guiEditFixture"
                $editDesktopDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $editDesktopDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeEditorSave)
                if ($bobMarkerCount -le $bobMarkerCountBeforeEditorSave) {
                    throw "The desktop could not open the editor-save fixture.`n$serialText"
                }
                Start-Sleep -Milliseconds 250
                Send-MonitorCommand 'sendkey end'
                Send-MonitorCommand 'sendkey spc'
                foreach ($key in @('s','a','v','e','d')) {
                    Send-MonitorCommand "sendkey $key"
                }
                Send-MonitorCommand 'sendkey ctrl-s'
                Start-Sleep -Milliseconds 150
                Send-MonitorCommand 'sendkey esc'
                $editorSaveExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $editorSaveExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeEditorSave -or
                          $exitMarkerCount -le $exitMarkerCountBeforeEditorSave))
                if ($shellMarkerCount -le $shellMarkerCountBeforeEditorSave -or
                    $exitMarkerCount -le $exitMarkerCountBeforeEditorSave) {
                    throw "Ctrl+S did not clear the editor's dirty state before Escape.`n$serialText"
                }
                Send-GuestText "cat $guiEditFixture"
                $editorSaveVerifyDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $editorSaveVerifyDeadline -and
                         $serialText -notmatch '(?m)^editable saved\r?$')
                if ($serialText -notmatch '(?m)^editable saved\r?$') {
                    throw "The editor did not save the exact edited text.`n$serialText"
                }
                Send-GuestText "rm $guiEditFixture"
                $editorSaveCleanupDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $editorSaveCleanupDeadline -and
                         -not $serialText.Contains('deleted'))
                if (-not $serialText.Contains('deleted')) {
                    throw "The editor-save fixture could not be cleaned up.`n$serialText"
                }
                Write-Output '  desktop editor: text insertion, Ctrl+S, clean Escape exit, and exact shell readback passed'
                $launcherSmokeStarted = $true
            }
            if ($allPresent -and -not $snapshotSaved) {
                if ($DiskSnapshotTest -and
                    (-not $serialText.Contains('GPT bob64 data partition found') -or
                     -not $serialText.Contains('dedicated data partition enables writes'))) {
                    throw "The kernel did not enable the prepared bob64 GPT partition.`n$serialText"
                }
                Send-GuestText 'write persist.txt survived'
                $writeDeadline = [DateTime]::UtcNow.AddSeconds(5)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $writeDeadline -and
                         -not $serialText.Contains('write persist.txt survived'))
                if (-not $serialText.Contains('write persist.txt survived')) {
                    throw "Could not enter the persistence smoke file through the QEMU monitor.`n$serialText"
                }
                Send-GuestText 'save'
                $saveTimeout = if ($DiskSnapshotTest) { 90 } else { 10 }
                $saveDeadline = [DateTime]::UtcNow.AddSeconds($saveTimeout)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $saveDeadline -and
                    -not $serialText.Contains('saved B64S v1 checkpoint to persistent storage and RAM'))
                if (-not $serialText.Contains('saved B64S v1 checkpoint to persistent storage and RAM')) {
                    throw "Persistent snapshot save failed in the guest.`n$serialText"
                }
                if ($DiskSnapshotTest -and
                    -not $serialText.Contains('disk B64S snapshot saved')) {
                    throw "The shell save did not commit a disk-backed B64S snapshot.`n$serialText"
                }
                $persistentMarkerCountBeforeReset =
                    [regex]::Matches($serialText, [regex]::Escape($persistentMarker)).Count
                $shellMarkerCountBeforeReset =
                    [regex]::Matches($serialText, [regex]::Escape($shellMarker)).Count
                Send-MonitorCommand 'system_reset'
                $snapshotSaved = $true
            }
            $persistentMarkerCount =
                [regex]::Matches($serialText, [regex]::Escape($persistentMarker)).Count
            if ($snapshotSaved -and
                $persistentMarkerCount -gt $persistentMarkerCountBeforeReset -and
                -not $restoredCompiledAppChecked) {
                if ($DiskSnapshotTest -and
                    -not $serialText.Contains('disk B64S snapshot restored')) {
                    throw "The reboot did not restore B64S from the USB GPT partition.`n$serialText"
                }
                $restoredShellDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape($shellMarker)).Count
                } while ([DateTime]::UtcNow -lt $restoredShellDeadline -and
                         $shellMarkerCount -le $shellMarkerCountBeforeReset)
                if ($shellMarkerCount -le $shellMarkerCountBeforeReset) {
                    throw "The shell did not resume after restoring the B64S snapshot.`n$serialText"
                }
                $bobMarkerCountBeforeRestoreRun =
                    [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                Send-GuestText 'run compiled.b64e'
                $restoreRunDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $restoreRunDeadline -and
                         (-not $serialText.Contains('application exit status=0x0000000000000000') -or
                          $bobMarkerCount -le $bobMarkerCountBeforeRestoreRun))
                if (-not $serialText.Contains('application exit status=0x0000000000000000') -or
                    $bobMarkerCount -le $bobMarkerCountBeforeRestoreRun) {
                    throw "The B64S restore did not preserve the compiled C application.`n$serialText"
                }
                $restoredCompiledAppChecked = $true
            }
            if ($snapshotSaved -and $restoredCompiledAppChecked -and
                -not $restoredDesktopChecked) {
                $bobMarkerCountBeforeRestoredDesktop =
                    [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                $shellMarkerCountBeforeRestoredDesktop =
                    [regex]::Matches($serialText, [regex]::Escape($shellPromptMarker)).Count
                $exitMarkerCountBeforeRestoredDesktop = [regex]::Matches($serialText,
                    [regex]::Escape('application exit status=0x0000000000000000')).Count
                Send-GuestText 'run desktop.b64e'
                $restoredDesktopDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $restoredDesktopDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeRestoredDesktop)
                if ($bobMarkerCount -le $bobMarkerCountBeforeRestoredDesktop) {
                    throw "The GUI did not launch after B64S restore.`n$serialText"
                }
                Start-Sleep -Milliseconds 500
                Send-MonitorCommand 'sendkey p'
                Send-MonitorCommand 'sendkey a'
                $restoredLauncherDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $bobMarkerCount =
                        [regex]::Matches($serialText, [regex]::Escape('bob!')).Count
                } while ([DateTime]::UtcNow -lt $restoredLauncherDeadline -and
                         $bobMarkerCount -le $bobMarkerCountBeforeRestoredDesktop+1)
                if ($bobMarkerCount -le $bobMarkerCountBeforeRestoredDesktop+1) {
                    throw "The restored desktop launcher did not run its selected bob! app.`n$serialText"
                }
                Send-MonitorCommand 'sendkey esc'
                Start-Sleep -Milliseconds 200
                Send-MonitorCommand 'sendkey esc'
                Start-Sleep -Milliseconds 200
                Send-MonitorCommand 'sendkey esc'
                $restoredDesktopExitDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                    $shellMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape($shellPromptMarker)).Count
                    $exitMarkerCount = [regex]::Matches($serialText,
                        [regex]::Escape('application exit status=0x0000000000000000')).Count
                } while ([DateTime]::UtcNow -lt $restoredDesktopExitDeadline -and
                         ($shellMarkerCount -le $shellMarkerCountBeforeRestoredDesktop -or
                          $exitMarkerCount -le $exitMarkerCountBeforeRestoredDesktop))
                if ($shellMarkerCount -le $shellMarkerCountBeforeRestoredDesktop -or
                    $exitMarkerCount -le $exitMarkerCountBeforeRestoredDesktop) {
                    throw "The mouse close button did not close the launcher before one Escape returned the desktop to the shell.`n$serialText"
                }
                $restoredDesktopChecked = $true
            }
            if ($snapshotSaved -and
                $persistentMarkerCount -gt $persistentMarkerCountBeforeReset -and
                $restoredCompiledAppChecked -and $restoredDesktopChecked -and
                $nativeLauncherSmokeStarted) {
                Write-Output 'UEFI runtime checks passed:'
                foreach ($marker in $requiredMarkers) {
                    Write-Output "  $marker"
                }
                Write-Output "  $persistentMarker"
                Write-Output '  bob! live mouse move, wheel, and managed-button events passed'
                Write-Output '  native graphics demo: title-bar drag moved the window; mouse Close returned to shell'
                Write-Output '  native windowed launcher: outside release cancelled Close; inside click closed it after bob! child'
                Write-Output '  launcher command: forwarded argv to echo.b64e and returned to shell'
                Write-Output '  ls gui: double-click opened bob.c in cat and returned through both windows'
                Write-Output '  cat gui: opened bob.c, accepted scroll keys and returned to shell'
                Write-Output '  info gui: refreshed system stats and Escape returned to shell'
                Write-Output '  exception diagnostics: 64-bit R15, RSP, SS and high CR2 printed; user faults recovered'
                Write-Output '  resident C compile/run: bob!, exit 0; unsigned long long aliases/calls/comparisons, switch/case fallthrough, do-while/continue, delayed local initialization, prefix/postfix increment and decrement, typed pointer steps, typedefs, scalar/pointer casts, division, bitwise/shift precedence, void-pointer conversions, 16-argument call, mixed register/stack pointer args with nested call, and large local frame passed'
                Write-Output '  desktop Applications launcher: child bob! returned to shell'
                Write-Output '  desktop filename field: mouse caret, Home/Right/End/Delete/Backspace, Ctrl+S Save As, shell file verification passed'
                Write-Output '  desktop file browser: Enter ran bob.b64e and returned to the open desktop'
                Write-Output '  post-restore compiled app launch: bob! and exit status 0'
                Write-Output '  post-restore desktop: launcher ran bob!, child and windows closed, Escape returned to shell'
                if ($DiskSnapshotTest) {
                    Stop-QemuForStorageCheck
                    $stream = [IO.File]::OpenRead($diskSnapshotImagePath)
                    try {
                        $snapshotPartitionFirstLba = if ($LargeLbaDiskSnapshotTest) {
                            [long]4294969344
                        } else { [long]2048 }
                        [void]$stream.Seek($snapshotPartitionFirstLba * 512L,
                            [IO.SeekOrigin]::Begin)
                        $slotHeader = New-Object byte[] 512
                        if ($stream.Read($slotHeader,0,$slotHeader.Length) -ne $slotHeader.Length -or
                            [Text.Encoding]::ASCII.GetString($slotHeader,0,4) -ne 'B64D') {
                            throw 'The raw disk image does not contain a committed B64D slot header.'
                        }
                        [void]$stream.Seek(($snapshotPartitionFirstLba + 1L) * 512L,
                            [IO.SeekOrigin]::Begin)
                        $snapshotHeader = New-Object byte[] 4
                        if ($stream.Read($snapshotHeader,0,4) -ne 4 -or
                            [Text.Encoding]::ASCII.GetString($snapshotHeader,0,4) -ne 'B64S') {
                            throw 'The raw disk image does not contain a B64S snapshot payload.'
                        }
                    } finally { $stream.Dispose() }
                    if ($LargeLbaDiskSnapshotTest) {
                        Write-Output '  USB GPT B64S persistence above 2 TiB: save, reboot restore, and raw-image verification passed'
                    } else {
                        Write-Output '  USB GPT B64S persistence: save, reboot restore, and raw-image verification passed'
                    }
                }
                & (Join-Path $PSScriptRoot 'test_bob64_serial.ps1') `
                    -QemuPath $QemuPath -OvmfCodePath $OvmfCodePath `
                    -OvmfVarsPath $varsTemplatePath -ImageRoot $ImageRoot `
                    -TimeoutSeconds $TimeoutSeconds
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
catch {
    Write-Output $_.InvocationInfo.PositionMessage
    Write-Output $_.ScriptStackTrace
    throw
}
finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    if (Test-Path -LiteralPath $serialPath) {
        Remove-Item -LiteralPath $serialPath -Force
    }
    if (Test-Path -LiteralPath $framebufferPath) {
        Remove-Item -LiteralPath $framebufferPath -Force
    }
    if (Test-Path -LiteralPath $varsCopyPath) {
        Remove-Item -LiteralPath $varsCopyPath -Force
    }
    if (Test-Path -LiteralPath $storageImagePath) {
        Remove-Item -LiteralPath $storageImagePath -Force
    }
    if (Test-Path -LiteralPath $storageReconnectImagePath) {
        Remove-Item -LiteralPath $storageReconnectImagePath -Force
    }
    if (Test-Path -LiteralPath $diskSnapshotImagePath) {
        Remove-Item -LiteralPath $diskSnapshotImagePath -Force
    }
}
