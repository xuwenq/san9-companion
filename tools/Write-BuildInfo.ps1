param([Parameter(Mandatory = $true)][string]$Output)
$ErrorActionPreference = 'Stop'
$displayDate = [DateTimeOffset]::Now.ToOffset([TimeSpan]::FromHours(8)).ToString('yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture)
$content = "#pragma once`nnamespace app::build_info`n{`ninline constexpr wchar_t CommitDate[] = L`"$displayDate`";`n}`n"
$outputPath = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
if (-not [IO.File]::Exists($outputPath) -or [IO.File]::ReadAllText($outputPath) -cne $content) {
    [IO.File]::WriteAllText($outputPath, $content, [Text.UTF8Encoding]::new($false))
}
