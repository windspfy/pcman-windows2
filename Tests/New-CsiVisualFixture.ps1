# Generates an offline fixture for PCMan's ANSI editor. No network access.
$ErrorActionPreference = 'Stop'
$esc = [char]27
$content = [Text.StringBuilder]::new()
[void]$content.Append("${esc}[0m${esc}[2J${esc}[1;1HPCMan CSI compatibility - offline visual check")
[void]$content.Append("${esc}[3;1H${esc}[0;31mRED BEFORE | ${esc}[>4;2mRED AFTER (both must stay red)")
[void]$content.Append("${esc}[4;1H${esc}[0;32mGREEN LEFT | ${esc}[1 AGREEN RIGHT (same row)")
[void]$content.Append("${esc}[5;1H${esc}[0;36mCYAN LEFT | ${esc}[>1;1HCYAN RIGHT (same row)")
[void]$content.Append("${esc}[6;1H${esc}[0mDEC2026: ${esc}[?2026hBEGIN-END${esc}[?2026l (no extra characters)")
[void]$content.Append("${esc}[7;1HMOUSE: ${esc}[?1000h${esc}[?1002h${esc}[?1003h${esc}[?1006hOK${esc}[?1000l${esc}[?1002l${esc}[?1003l${esc}[?1006l")
[void]$content.Append("${esc}[8;1HLONG CSI: ${esc}[" + ('9' * 100) + 'mOK (no digits printed)')
[void]$content.Append("${esc}[9;1HRESTART: ${esc}[31${esc}[0;32mGREEN OK${esc}[0m (no leftover 32m)")
[void]$content.Append("${esc}[10;1HCPR: ${esc}[6nOK (no extra characters)")
[void]$content.Append("${esc}[12;1HNormal color/cursor commands must still work.")
[void]$content.Append("${esc}[13;1HOpen this file locally; do not paste/send it to a BBS.")
[void]$content.Append("${esc}[0m${esc}[15;1HEND")
$directory = Join-Path $PSScriptRoot 'fixtures'
[void][IO.Directory]::CreateDirectory($directory)
$outputPath = Join-Path $directory 'CsiCompatibility.ans'
[IO.File]::WriteAllBytes($outputPath, [Text.Encoding]::ASCII.GetBytes($content.ToString()))
Write-Output $outputPath
