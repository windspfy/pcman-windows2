param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '../Lite/TelnetConn.cpp'), [Text.Encoding]::GetEncoding(950))
$parts = @()
foreach ($bounds in @(
    @('int CTelnetConn::IsEndOfArticleReached()', 'CString CTelnetConn::GetLineWithAnsi'),
    @('void CTelnetConn::CopyArticle(bool', 'void CTelnetConn::CopyArticleComplete(bool')
)) {
    $start = $source.IndexOf($bounds[0], [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing method: $($bounds[0])" }
    $end = $source.IndexOf($bounds[1], $start, [StringComparison]::Ordinal)
    if ($end -lt 0) { throw "Missing boundary: $($bounds[1])" }
    $parts += $source.Substring($start, $end - $start)
}
$fullPath = [IO.Path]::GetFullPath($OutputPath)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fullPath))
[IO.File]::WriteAllText($fullPath, ($parts -join "`r`n"), [Text.UTF8Encoding]::new($false))
