# restore_deepseek.ps1 —— 切回 deepseek 配置（用户级，持久生效）
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File restore_deepseek.ps1

# 先清掉中转接口相关变量（避免残留）
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_BASE_URL", $null, "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_AUTH_TOKEN", $null, "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_MODEL", $null, "User")
[System.Environment]::SetEnvironmentVariable("OPENAI_API_KEY", $null, "User")
[System.Environment]::SetEnvironmentVariable("OPENAI_BASE_URL", $null, "User")

# 恢复 deepseek 配置
# API key 从环境变量读取，不再硬编码（避免密钥随仓库泄漏）。
# 先设 DEEPSEEK_API_KEY，或在调用前用 $env:DEEPSEEK_API_KEY 提供。
$deepseekKey = $env:DEEPSEEK_API_KEY
if (-not $deepseekKey) {
    throw "未找到 DEEPSEEK_API_KEY。请先设置： `$env:DEEPSEEK_API_KEY = 'sk-...'，或用 setx DEEPSEEK_API_KEY 'sk-...' 持久化。"
}
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_BASE_URL", 'https://api.deepseek.com/anthropic', "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_AUTH_TOKEN", $deepseekKey, "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_MODEL", 'deepseek-v4-pro[1m]', "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_DEFAULT_OPUS_MODEL", 'deepseek-v4-pro[1m]', "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_DEFAULT_SONNET_MODEL", 'deepseek-v4-pro[1m]', "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_DEFAULT_HAIKU_MODEL", 'deepseek-v4-flash', "User")
[System.Environment]::SetEnvironmentVariable("CLAUDE_CODE_ENTRYPOINT", 'cli', "User")

Write-Host "已切回 deepseek，重启终端后生效。"
Write-Host "  ANTHROPIC_BASE_URL = https://api.deepseek.com/anthropic"
Write-Host "  ANTHROPIC_MODEL    = deepseek-v4-pro[1m]"
