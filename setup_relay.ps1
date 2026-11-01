# setup_relay.ps1 —— 配置第三方中转接口（Claude Code 为主，附带 OpenAI SDK）
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File setup_relay.ps1 -ApiKey "sk-完整Key"
param(
    [Parameter(Mandatory = $true)][string]$ApiKey
)

$ErrorActionPreference = 'Stop'

# 3. Claude Code 专用
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_BASE_URL", "https://rsxermu666.cn", "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_AUTH_TOKEN", $ApiKey, "User")
[System.Environment]::SetEnvironmentVariable("ANTHROPIC_MODEL", "claude-fable-5-1", "User")

# 5. OpenAI SDK（Codex CLI 复用 OPENAI_API_KEY，OPENAI_BASE_URL 按需）
[System.Environment]::SetEnvironmentVariable("OPENAI_API_KEY", $ApiKey, "User")
[System.Environment]::SetEnvironmentVariable("OPENAI_BASE_URL", "https://rsxermu666.cn/openai", "User")

Write-Host ""
Write-Host "已配置完成，请重启终端后生效。"
Write-Host "  ANTHROPIC_BASE_URL = https://rsxermu666.cn"
Write-Host "  ANTHROPIC_MODEL    = claude-fable-5-1"
Write-Host "  ANTHROPIC_AUTH_TOKEN / OPENAI_API_KEY 已写入（已隐藏）"
Write-Host ""
Write-Host "验证："
Write-Host "  [System.Environment]::GetEnvironmentVariable('ANTHROPIC_BASE_URL','User')"
Write-Host "  [System.Environment]::GetEnvironmentVariable('ANTHROPIC_MODEL','User')"
