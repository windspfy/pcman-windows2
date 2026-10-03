# Local manual visual fixture. No Internet access, credentials, or input logging.
param([ValidateRange(1024,65535)][int]$Port = 22326)
$ErrorActionPreference = 'Stop'
$esc = [char]27
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)
$client = $null
function Send-Text([string]$Text) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($Text)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
}
function Send-Rows([int]$First, [int]$Last, [string]$Label, [int]$Color) {
    for($row=$First; $row -le $Last; ++$row) {
        Send-Text ("${esc}[${row};1H${esc}[${Color}m" + ('{0,-60}' -f "$Label - row $row"))
    }
}
function Baseline([string]$Title) {
    Send-Text "${esc}[0m${esc}[2J${esc}[1;1H$Title"
    Send-Rows 3 20 'OLD' 32
    Send-Text "${esc}[0m${esc}[23;1HLocal test only. No login required."
    Start-Sleep -Milliseconds 500
}
try {
    $listener.Start()
    Write-Host "Use a NEW PCMan Telnet connection: 127.0.0.1:$Port"
    Write-Host 'No saved login/macros/triggers. Do not enter credentials.'
    Write-Host 'Waiting at most 60 seconds...'
    $accept = $listener.AcceptTcpClientAsync()
    if(-not $accept.Wait(60000)) { throw 'No connection within 60 seconds.' }
    $client = $accept.GetAwaiter().GetResult()
    $client.NoDelay = $true
    $stream = $client.GetStream()

    Baseline 'A: NO SYNC - expect mixed OLD/NEW for one second'
    [void](Read-Host 'A ready. Press Enter to run')
    Send-Rows 3 10 'NEW' 33
    Start-Sleep -Milliseconds 1000
    Send-Rows 11 20 'NEW' 33

    [void](Read-Host 'B: Press Enter, then watch PCMan (three passes)')
    for($pass=1; $pass -le 3; ++$pass) {
        Baseline "B: SYNC pass $pass - OLD then all NEW, no mixed rows"
        Send-Text "${esc}[?2026h"
        Send-Rows 3 10 'NEW' 33
        Start-Sleep -Milliseconds 1000
        Send-Rows 11 20 'NEW' 33
        Send-Text "${esc}[?2026l"
        Start-Sleep -Milliseconds 1000
    }

    [void](Read-Host 'C: Press Enter to test missing END and timeout')
    Baseline 'C: Missing END - OLD briefly, then partial NEW after timeout'
    Send-Text "${esc}[?2026h"
    Send-Rows 3 10 'NEW (TIMEOUT)' 31
    Start-Sleep -Milliseconds 4500
    Send-Rows 11 20 'RECOVERED' 36
    Send-Text "${esc}[?2026l"

    [void](Read-Host 'D: Press Enter, then resize/switch tabs during the three passes')
    for($pass=1; $pass -le 3; ++$pass) {
        Baseline "D: REPAINT pass $pass - same OLD then NEW rule"
        Send-Text "${esc}[?2026h"
        Send-Rows 3 10 'NEW' 35
        Start-Sleep -Milliseconds 1400
        Send-Rows 11 20 'NEW' 35
        Send-Text "${esc}[?2026l"
        Start-Sleep -Milliseconds 1000
    }

    [void](Read-Host 'E: Press Enter to disconnect in the middle of a frame')
    Baseline 'E: Disconnect mid-frame - must not remain frozen'
    Send-Text "${esc}[?2026h"
    Send-Rows 3 10 'DISCONNECTED' 31
    Start-Sleep -Milliseconds 500
    Write-Host 'Done. Rerun this script before reconnecting. Close the test tab.'
}
finally {
    if($client) { $client.Dispose() }
    $listener.Stop()
}
