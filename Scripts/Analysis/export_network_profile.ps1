param(
    [Parameter(Mandatory = $true)]
    [string]$ProfilePath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$NamePattern = 'RecoverableFall|ServerRequestRecovery|FallState|RecoveryBranch'
)

$ErrorActionPreference = 'Stop'

function Read-AnsiString {
    param([System.IO.BinaryReader]$Reader)
    $length = $Reader.ReadInt32()
    if ($length -lt 0 -or $length -gt 1048576) {
        throw "Invalid ANSI string length $length at offset $($Reader.BaseStream.Position - 4)."
    }
    return [System.Text.Encoding]::Latin1.GetString($Reader.ReadBytes($length)).TrimEnd([char]0)
}

function Read-PackedUInt32 {
    param([System.IO.BinaryReader]$Reader)
    [uint32]$value = 0
    [int]$shift = 0
    do {
        if ($shift -gt 28) { throw "Packed uint32 exceeds five bytes at offset $($Reader.BaseStream.Position)." }
        [byte]$next = $Reader.ReadByte()
        $value = $value -bor ([uint32]($next -shr 1) -shl $shift)
        $shift += 7
    } while (($next -band 1) -ne 0)
    return $value
}

function Resolve-Name {
    param([System.Collections.Generic.List[string]]$Names, [uint32]$Index, [switch]$AllowUnresolved)
    if ($Index -ge $Names.Count) {
        if (-not $AllowUnresolved) { throw "Name index $Index is outside table size $($Names.Count)." }
        $script:UnresolvedNameCount++
        return "<unresolved:$Index>"
    }
    return $Names[[int]$Index]
}

$resolvedProfile = (Resolve-Path -LiteralPath $ProfilePath).Path
$stream = [System.IO.File]::Open($resolvedProfile, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
$reader = [System.IO.BinaryReader]::new($stream)
$names = [System.Collections.Generic.List[string]]::new()
$connections = [System.Collections.Generic.List[string]]::new()
$events = [System.Collections.Generic.List[object]]::new()
$pendingContentEvents = [System.Collections.Generic.List[object]]::new()
$tokenCounts = @{}
$currentConnection = ''
[single]$currentTime = 0
$sawEnd = $false
$script:UnresolvedNameCount = 0

try {
    $magic = $reader.ReadUInt32()
    $version = $reader.ReadUInt32()
    if ($magic -ne 0x1DBF348C) { throw ('Invalid Network Profiler magic 0x{0:X8}.' -f $magic) }
    if ($version -ne 14) { throw "Unsupported Network Profiler version $version; this parser is locked to UE 5.4 version 14." }

    $header = [ordered]@{
        magic = ('0x{0:X8}' -f $magic)
        version = $version
        tag = Read-AnsiString $reader
        game_name = Read-AnsiString $reader
        url = Read-AnsiString $reader
    }

    while ($reader.BaseStream.Position -lt $reader.BaseStream.Length) {
        $offset = $reader.BaseStream.Position
        [byte]$type = $reader.ReadByte()
        $key = [string]$type
        if (-not $tokenCounts.ContainsKey($key)) { $tokenCounts[$key] = 0 }
        $tokenCounts[$key]++

        switch ($type) {
            0 { $currentTime = $reader.ReadSingle() }
            1 {
                $socketName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $bytesSent = $reader.ReadUInt16()
                $packetIdBits = $reader.ReadUInt16()
                $bunchBits = $reader.ReadUInt16()
                $ackBits = $reader.ReadUInt16()
                $paddingBits = $reader.ReadUInt16()
                $events.Add([ordered]@{ type='SocketSendTo'; time_s=$currentTime; connection=$currentConnection; name=$socketName; bytes=$bytesSent; packet_id_bits=$packetIdBits; bunch_bits=$bunchBits; ack_bits=$ackBits; padding_bits=$paddingBits; offset=$offset })
            }
            2 {
                $channelIndex = $reader.ReadUInt16()
                # A seamless-travel session can flush one queued channel entry whose table index
                # belonged to the preceding .nprof file. UE resets the name table but not that queue.
                $channelType = Resolve-Name $names (Read-PackedUInt32 $reader) -AllowUnresolved
                $headerBits = $reader.ReadUInt16()
                $payloadBits = $reader.ReadUInt16()
                $events.Add([ordered]@{ type='SendBunch'; time_s=$currentTime; connection=$currentConnection; channel_index=$channelIndex; channel_type=$channelType; header_bits=$headerBits; payload_bits=$payloadBits; total_bits=($headerBits+$payloadBits); offset=$offset })
            }
            3 {
                $actorName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $functionName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $headerBits = Read-PackedUInt32 $reader
                $parameterBits = Read-PackedUInt32 $reader
                $footerBits = Read-PackedUInt32 $reader
                $events.Add([ordered]@{ type='SendRPC'; time_s=$currentTime; connection=$currentConnection; actor=$actorName; function=$functionName; header_bits=$headerBits; parameter_bits=$parameterBits; footer_bits=$footerBits; total_bits=($headerBits+$parameterBits+$footerBits); offset=$offset })
            }
            4 {
                $flags = $reader.ReadByte()
                $actorName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $timeMs = $reader.ReadSingle()
                $events.Add([ordered]@{ type='ReplicateActor'; time_s=$currentTime; connection=$currentConnection; actor=$actorName; flags=$flags; replication_time_ms=$timeMs; offset=$offset })
            }
            5 {
                $propertyName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $bits = $reader.ReadUInt16()
                $event = [pscustomobject][ordered]@{ type='ReplicateProperty'; time_s=$currentTime; connection=$currentConnection; content=''; property=$propertyName; payload_bits=$bits; offset=$offset }
                $events.Add($event)
                $pendingContentEvents.Add($event)
            }
            6 { $sawEnd = $true; break }
            7 {
                $eventName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $description = Resolve-Name $names (Read-PackedUInt32 $reader)
                $events.Add([ordered]@{ type='Event'; time_s=$currentTime; connection=$currentConnection; name=$eventName; description=$description; offset=$offset })
            }
            8 {
                $byteCount = $reader.ReadUInt16()
                $null = $reader.ReadBytes($byteCount)
            }
            9 { $null = $reader.ReadUInt16() }
            10 {
                $propertyName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $bits = $reader.ReadUInt16()
                $event = [pscustomobject][ordered]@{ type='WritePropertyHeader'; time_s=$currentTime; connection=$currentConnection; content=''; property=$propertyName; header_bits=$bits; offset=$offset }
                $events.Add($event)
                $pendingContentEvents.Add($event)
            }
            11 { $null = $reader.ReadUInt16() }
            12 { $null = $reader.ReadUInt16(); $null = $reader.ReadUInt16() }
            13 {
                $objectName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $bits = $reader.ReadUInt16()
                foreach ($pendingEvent in $pendingContentEvents) { $pendingEvent.content = $objectName }
                $pendingContentEvents.Clear()
                $events.Add([ordered]@{ type='BeginContentBlock'; time_s=$currentTime; connection=$currentConnection; content=$objectName; header_bits=$bits; offset=$offset })
            }
            14 {
                $objectName = Resolve-Name $names (Read-PackedUInt32 $reader)
                $bits = $reader.ReadUInt16()
                $events.Add([ordered]@{ type='EndContentBlock'; time_s=$currentTime; connection=$currentConnection; content=$objectName; footer_bits=$bits; offset=$offset })
            }
            15 {
                $bits = $reader.ReadUInt16()
                $event = [pscustomobject][ordered]@{ type='WritePropertyHandle'; time_s=$currentTime; connection=$currentConnection; content=''; bits=$bits; offset=$offset }
                $events.Add($event)
                $pendingContentEvents.Add($event)
            }
            16 {
                $connectionIndex = Read-PackedUInt32 $reader
                if ($connectionIndex -ge $connections.Count) { throw "Connection index $connectionIndex is outside table size $($connections.Count)." }
                $currentConnection = $connections[[int]$connectionIndex]
            }
            17 { $names.Add((Read-AnsiString $reader)) }
            18 { $connections.Add((Read-AnsiString $reader)) }
            19 { throw "PropertyComparison token at offset $offset requires comparison-data parsing; this capture must be regenerated with Net.ProfilerUseComparisonTracking=0 or the parser extended." }
            20 { throw "ReplicatePropertiesMetadata token at offset $offset requires comparison-data parsing; this capture must be regenerated with Net.ProfilerUseComparisonTracking=0 or the parser extended." }
            default { throw "Unknown Network Profiler token $type at offset $offset." }
        }
        if ($sawEnd) { break }
    }

    if (-not $sawEnd) { throw 'EndOfStream token was not found.' }
    if ($reader.BaseStream.Position -ne $reader.BaseStream.Length) { throw "Trailing bytes remain after EndOfStream: $($reader.BaseStream.Length - $reader.BaseStream.Position)." }

    $matching = @($events | Where-Object {
        (($_.PSObject.Properties.Value | ForEach-Object { [string]$_ }) -join ' ') -match $NamePattern
    })
    $rpc = @($events | Where-Object type -eq 'SendRPC')
    $properties = @($events | Where-Object type -eq 'ReplicateProperty')
    $sockets = @($events | Where-Object type -eq 'SocketSendTo')
    $matchingFrameSocketSummaries = @($matching |
        Where-Object { $_.type -in @('SendRPC', 'ReplicateProperty') } |
        ForEach-Object { [single]$_['time_s'] } |
        Sort-Object -Unique |
        ForEach-Object {
            [single]$sampleTime = $_
            $frameSockets = @($sockets | Where-Object { [single]$_['time_s'] -eq $sampleTime })
            [pscustomobject][ordered]@{
                time_s = $sampleTime
                socket_send_count = $frameSockets.Count
                socket_wire_bytes = [long](($frameSockets | ForEach-Object { [long]$_['bytes'] } | Measure-Object -Sum).Sum)
            }
        })
    $result = [ordered]@{
        generated_at = (Get-Date).ToString('o')
        source = $resolvedProfile
        file_bytes = $reader.BaseStream.Length
        parser_contract = 'UE 5.4 Network Profiler version 14; complete EndOfStream required; read-only.'
        header = $header
        parsed = [ordered]@{
            end_of_stream = $sawEnd
            name_count = $names.Count
            connection_count = $connections.Count
            unresolved_name_references = $script:UnresolvedNameCount
            token_counts = $tokenCounts
            event_count = $events.Count
            rpc_count = $rpc.Count
            replicated_property_count = $properties.Count
            socket_send_count = $sockets.Count
            socket_wire_bytes = [long](($sockets | ForEach-Object { [long]$_['bytes'] } | Measure-Object -Sum).Sum)
        }
        filter = $NamePattern
        matching_events = $matching
        matching_frame_socket_summaries = $matchingFrameSocketSummaries
    }
    $outputDirectory = Split-Path -Parent $OutputPath
    if ($outputDirectory) { New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null }
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    $result | ConvertTo-Json -Depth 5
}
finally {
    $reader.Dispose()
    $stream.Dispose()
}
