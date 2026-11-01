$names = 'ANTHROPIC_BASE_URL','ANTHROPIC_AUTH_TOKEN','ANTHROPIC_MODEL','ANTHROPIC_DEFAULT_OPUS_MODEL','ANTHROPIC_DEFAULT_SONNET_MODEL','ANTHROPIC_DEFAULT_HAIKU_MODEL','CLAUDE_CODE_ENTRYPOINT'
Write-Host '--- Process scope (this Claude Code process) ---'
foreach ($n in $names) {
    $v = [System.Environment]::GetEnvironmentVariable($n, 'Process')
    if ($n -match 'TOKEN') { if ($v) { $v = $v.Substring(0, [Math]::Min(10, $v.Length)) + '...(len=' + $v.Length + ')' } }
    Write-Host ("{0,-30} = {1}" -f $n, $v)
}
Write-Host '--- User scope (registry) ---'
foreach ($n in $names) {
    $v = [System.Environment]::GetEnvironmentVariable($n, 'User')
    if ($n -match 'TOKEN') { if ($v) { $v = $v.Substring(0, [Math]::Min(10, $v.Length)) + '...(len=' + $v.Length + ')' } }
    Write-Host ("{0,-30} = {1}" -f $n, $v)
}
