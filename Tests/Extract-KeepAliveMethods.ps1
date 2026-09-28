param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
# Test the real receive/negotiation/raw-send implementations with a fake
# transport and text sink. Generated output stays in the ignored build tree.
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '../Lite/TelnetConn.cpp'), [Text.Encoding]::GetEncoding(950))
$parts = @()
foreach ($bounds in @(
    @('inline void CTelnetConn::OnIAC()', 'class CDownloadArticleDlg'),
    @('inline void CTelnetConn::ProcessData(int len)', 'void CTelnetConn::LocalEcho'),
    @('int CTelnetConn::Send(const void *lpBuf, int nBufLen)', 'const char* memstr')
)) {
    $start = $source.IndexOf($bounds[0], [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Source method missing: $($bounds[0])" }
    $end = $source.IndexOf($bounds[1], $start, [StringComparison]::Ordinal)
    if ($end -lt 0) { throw "Source boundary missing: $($bounds[1])" }
    $parts += $source.Substring($start, $end - $start)
}
$fullPath = [IO.Path]::GetFullPath($OutputPath)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fullPath))
[IO.File]::WriteAllText($fullPath, ($parts -join "`r`n"), [Text.UTF8Encoding]::new($false))
