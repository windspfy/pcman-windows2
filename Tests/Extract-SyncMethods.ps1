param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
$bytes = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot '../Lite/TelnetConn.cpp'))
try { $source = [Text.UTF8Encoding]::new($false, $true).GetString($bytes) }
catch { $source = [Text.Encoding]::GetEncoding(950).GetString($bytes) }
$start = $source.IndexOf('void CTelnetConn::BeginSynchronizedOutput()', [StringComparison]::Ordinal)
if ($start -lt 0) { throw 'Missing sync methods' }
$end = $source.IndexOf('void CTelnetConn::OnClose()', $start, [StringComparison]::Ordinal)
if ($end -le $start) { throw 'Missing sync methods' }
$fullPath = [IO.Path]::GetFullPath($OutputPath)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fullPath))
[IO.File]::WriteAllText($fullPath, $source.Substring($start, $end - $start), [Text.UTF8Encoding]::new($false))
