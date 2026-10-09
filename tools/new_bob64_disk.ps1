param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [ValidateRange(32, 2097154)]
    [int]$SizeMiB = 64,
    [long]$FirstPartitionLba = 2048,
    [switch]$Sparse,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$sectorSize = 512
$sectorCount = [uint64]$SizeMiB * 1MB / $sectorSize
$tableBytes = 128 * 128
$tableSectors = [uint64][Math]::Ceiling($tableBytes / $sectorSize)
if ($sectorCount -lt (4096 + 2 * $tableSectors + 2)) {
    throw 'The image is too small for the bob64 data partition and GPT metadata.'
}

$fullPath = [IO.Path]::GetFullPath($Path)
if ((Test-Path -LiteralPath $fullPath) -and -not $Force) {
    throw "Refusing to overwrite '$fullPath'; pass -Force to replace it."
}
$parent = Split-Path -Parent $fullPath
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    throw "Output directory does not exist: $parent"
}

function Set-U16([byte[]]$Buffer,[int]$Offset,[uint16]$Value) {
    [Buffer]::BlockCopy([BitConverter]::GetBytes($Value),0,$Buffer,$Offset,2)
}
function Set-U32([byte[]]$Buffer,[int]$Offset,[uint32]$Value) {
    [Buffer]::BlockCopy([BitConverter]::GetBytes($Value),0,$Buffer,$Offset,4)
}
function Set-U64([byte[]]$Buffer,[int]$Offset,[uint64]$Value) {
    [Buffer]::BlockCopy([BitConverter]::GetBytes($Value),0,$Buffer,$Offset,8)
}
function Get-Crc32([byte[]]$Bytes) {
    [uint32]$crc = [uint32]::MaxValue
    foreach ($value in $Bytes) {
        $crc = $crc -bxor [uint32]$value
        for ($bit = 0; $bit -lt 8; $bit++) {
            if ($crc -band 1) { $crc = ($crc -shr 1) -bxor 0xedb88320 }
            else { $crc = $crc -shr 1 }
        }
    }
    return [uint32]($crc -bxor 0xffffffff)
}
function New-GptHeader([uint64]$CurrentLba,[uint64]$BackupLba,
        [uint64]$TableLba,[uint64]$FirstUsable,[uint64]$LastUsable,
        [byte[]]$DiskGuid,[uint32]$TableCrc) {
    $header = [byte[]]::new($sectorSize)
    [Text.Encoding]::ASCII.GetBytes('EFI PART').CopyTo($header,0)
    Set-U32 $header 8 0x00010000
    Set-U32 $header 12 92
    Set-U64 $header 24 $CurrentLba
    Set-U64 $header 32 $BackupLba
    Set-U64 $header 40 $FirstUsable
    Set-U64 $header 48 $LastUsable
    [Buffer]::BlockCopy($DiskGuid,0,$header,56,16)
    Set-U64 $header 72 $TableLba
    Set-U32 $header 80 128
    Set-U32 $header 84 128
    Set-U32 $header 88 $TableCrc
    $crcInput = [byte[]]::new(92)
    [Buffer]::BlockCopy($header,0,$crcInput,0,92)
    Set-U32 $header 16 (Get-Crc32 $crcInput)
    return ,$header
}

$table = [byte[]]::new([int]($tableSectors * $sectorSize))
$typeGuid = [byte[]](0x64,0x62,0x61,0x7b,0x30,0x30,0x34,0x30,
    0x9a,0x21,0x42,0x4f,0x42,0x36,0x34,0x01)
$uniqueGuid = [Guid]::NewGuid().ToByteArray()
$firstPartitionLba = [uint64]$FirstPartitionLba
$lastUsableLba = $sectorCount - $tableSectors - 2
if ($firstPartitionLba -lt 2048 -or $firstPartitionLba -gt $lastUsableLba) {
    throw 'The data partition start must be inside the GPT usable-LBA range.'
}
$lastPartitionLba = $sectorCount - $tableSectors - 2
[Buffer]::BlockCopy($typeGuid,0,$table,0,16)
[Buffer]::BlockCopy($uniqueGuid,0,$table,16,16)
Set-U64 $table 32 $firstPartitionLba
Set-U64 $table 40 $lastPartitionLba
$name = [Text.Encoding]::Unicode.GetBytes('bob64 data')
[Buffer]::BlockCopy($name,0,$table,56,$name.Length)
$tableCrc = Get-Crc32 $table
$diskGuid = [Guid]::NewGuid().ToByteArray()
$primaryHeader = New-GptHeader 1 ($sectorCount - 1) 2 34 $lastUsableLba $diskGuid $tableCrc
$backupTableLba = $sectorCount - $tableSectors - 1
$backupHeader = New-GptHeader ($sectorCount - 1) 1 $backupTableLba 34 $lastUsableLba $diskGuid $tableCrc
$mbr = [byte[]]::new($sectorSize)
$mbr[446 + 4] = 0xee
Set-U32 $mbr (446 + 8) 1
Set-U32 $mbr (446 + 12) ([uint32][Math]::Min([double]($sectorCount - 1),[double][uint32]::MaxValue))
$mbr[510] = 0x55
$mbr[511] = 0xaa

$stream = [IO.File]::Open($fullPath,[IO.FileMode]::Create,
    [IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    if ($Sparse) {
        $stream.Dispose()
        & fsutil.exe sparse setflag $fullPath | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not mark the GPT disk image as sparse.'
        }
        $stream = [IO.File]::Open($fullPath,[IO.FileMode]::Open,
            [IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    }
    $stream.SetLength([long]$sectorCount * $sectorSize)
    $stream.Position = 0; $stream.Write($mbr,0,$mbr.Length)
    $stream.Position = $sectorSize; $stream.Write($primaryHeader,0,$primaryHeader.Length)
    $stream.Position = 2 * $sectorSize; $stream.Write($table,0,$table.Length)
    $stream.Position = [long]$backupTableLba * $sectorSize
    $stream.Write($table,0,$table.Length)
    $stream.Position = [long]($sectorCount - 1) * $sectorSize
    $stream.Write($backupHeader,0,$backupHeader.Length)
    $stream.Flush($true)
} finally {
    if ($stream) { $stream.Dispose() }
}

$partitionBlocks = $lastPartitionLba - $firstPartitionLba + 1
Write-Output "Created bob64 GPT disk image: $fullPath"
Write-Output "Disk size: $SizeMiB MiB; data partition: LBA $firstPartitionLba, $partitionBlocks sectors"
Write-Output 'Attach this raw image to the emulator as a USB mass-storage disk.'
