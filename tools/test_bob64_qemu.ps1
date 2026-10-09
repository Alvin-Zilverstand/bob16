param(
    [Parameter(Mandatory = $true)]
    [string]$QemuPath,
    [Parameter(Mandatory = $true)]
    [string]$OvmfCodePath,
    [string]$OvmfVarsPath,
    [string]$ImageRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\bob64-handoff'),
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = 'Stop'
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
    'bob64 kernel: read-only code protection passed',
    'bob64 kernel: non-executable data protection passed',
    'bob64 kernel: memory checks passed',
    'bob64 kernel: timer IRQ enabled',
    'bob64 kernel: timer tick passed',
    'bob64 kernel: keyboard IRQ enabled',
    'bob64 kernel: PS/2 mouse IRQ enabled',
    'bob64! kernel shell; type help'
)
$persistentMarker = 'bob64 kernel: persistent B64S smoke file passed'
$mouseMarker = 'bob64 live mouse event passed'
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
$varsCopyPath = Join-Path ([IO.Path]::GetTempPath()) ("bob64-ovmf-vars-{0}.fd" -f [guid]::NewGuid())
Copy-Item -LiteralPath $varsTemplatePath -Destination $varsCopyPath

$portProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$monitorPort = ([Net.IPEndPoint]$portProbe.LocalEndpoint).Port
$portProbe.Stop()

function Send-MonitorCommand([string]$command) {
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
    }
    finally {
        $client.Dispose()
    }
}

function Send-GuestText([string]$text) {
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
            Start-Sleep -Milliseconds 125
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

$arguments = @(
    '-machine', 'q35', '-m', '512M', '-cpu', 'max', '-L', $shareDirectory,
    '-drive', "if=pflash,format=raw,unit=0,file=$firmwarePath,readonly=on",
    '-drive', "if=pflash,format=raw,unit=1,file=$varsCopyPath",
    '-drive', "format=raw,file=fat:rw:$imagePath",
    '-boot', 'order=c', '-display', 'none', '-serial', "file:$serialPath",
    '-monitor', "tcp:127.0.0.1:$monitorPort,server=on,wait=off"
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
                Send-MonitorCommand 'sendkey x'
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
                    throw "The native graphics demo did not return cleanly to the shell on X.`n$serialText"
                }
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
                # The first installed app is the interactive graphics demo;
                # Escape closes that child, then a second Escape closes the
                # launcher after its parent context resumes.
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey esc'
                Start-Sleep -Milliseconds 300
                Send-MonitorCommand 'sendkey esc'
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
                    throw "The native windowed launcher did not close cleanly on Escape.`n$serialText"
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
                # The PS/2 pointer is at (672, 400). Click the editor's name
                # field at (512, 60), replace its filename, and save as.
                Send-MonitorCommand 'mouse_move -53 -113'
                Send-MonitorCommand 'mouse_move -53 -113'
                Send-MonitorCommand 'mouse_move -54 -114'
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
                Send-MonitorCommand 'sendkey end'
                foreach ($index in 1..9) {
                    Send-MonitorCommand 'sendkey backspace'
                }
                Send-GuestText 'saved.txt'
                Send-MonitorCommand 'sendkey ctrl-s'
                Send-MonitorCommand 'sendkey tab'
                Start-Sleep -Milliseconds 200
                Send-GuestText 'pa'
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
                $launcherSmokeStarted = $true
            }
            if ($allPresent -and -not $snapshotSaved) {
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
                $saveDeadline = [DateTime]::UtcNow.AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 100
                    $serialText = [string](Get-Content -LiteralPath $serialPath -Raw)
                } while ([DateTime]::UtcNow -lt $saveDeadline -and
                    -not $serialText.Contains('saved B64S v1 checkpoint to firmware storage and RAM'))
                if (-not $serialText.Contains('saved B64S v1 checkpoint to firmware storage and RAM')) {
                    throw "Firmware snapshot save failed in the guest.`n$serialText"
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
                Send-GuestText 'p'
                Start-Sleep -Milliseconds 300
                # The mouse smoke app moved the PS/2 pointer 32 pixels from
                # center. Move from that known position to Run App in the
                # launcher at screen (994, 79), then click and release.
                Send-MonitorCommand 'mouse_move 107 -107'
                Send-MonitorCommand 'mouse_move 107 -107'
                Send-MonitorCommand 'mouse_move 108 -107'
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
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
                # The pointer remains at Run App after the child returns. The
                # adjacent X CLOSE control is 80 pixels to its right.
                Send-MonitorCommand 'mouse_move 80 0'
                Send-MonitorCommand 'mouse_button 1'
                Start-Sleep -Milliseconds 100
                Send-MonitorCommand 'mouse_button 0'
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
                Write-Output "  $mouseMarker"
                Write-Output '  native graphics demo: surface presented, bob!, X returned to shell'
                Write-Output '  native windowed launcher: ran the selected bob! app and Escape returned to shell'
                Write-Output '  launcher command: forwarded argv to echo.b64e and returned to shell'
                Write-Output '  resident C compile/run: bob!, exit 0; typedefs, division, bitwise/shift precedence, 16-argument call and large local frame passed'
                Write-Output '  desktop Applications launcher: child bob! returned to shell'
                Write-Output '  desktop filename field: mouse caret, keyboard edit, Ctrl+S Save As, shell file verification passed'
                Write-Output '  post-restore compiled app launch: bob! and exit status 0'
                Write-Output '  post-restore desktop: mouse ran bob! and clicked X CLOSE; Escape returned to shell'
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
    if (Test-Path -LiteralPath $varsCopyPath) {
        Remove-Item -LiteralPath $varsCopyPath -Force
    }
}
