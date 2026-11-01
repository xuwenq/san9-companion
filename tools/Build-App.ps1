param([ValidateSet('Debug','Release','Publish')][string]$Configuration = 'Debug', [switch]$OfflineTests, [switch]$UiSmoke, [ValidateSet('Full','Quick')][string]$TestSuite = 'Full')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'VisualStudio.ps1')
$vsPath = Get-VisualStudioPath
$msbuildPath = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
$logFolder = Join-Path $projectRoot 'out\app'
New-Item -ItemType Directory -Path $logFolder -Force | Out-Null
& $msbuildPath (Join-Path $projectRoot 'San9Companion.sln') /nologo /m /v:minimal `
    "/p:Configuration=$Configuration" /p:Platform=Win32 `
    /fl "/flp:logfile=$logFolder\$Configuration.log;verbosity=normal;encoding=UTF-8"
if ($LASTEXITCODE -ne 0) { throw "Application build failed: $LASTEXITCODE" }
if ($OfflineTests) {
    & $msbuildPath (Join-Path $projectRoot 'build-config\RuntimeTests.vcxproj') /nologo /m /v:minimal `
        "/p:Configuration=$Configuration" /p:Platform=Win32
    if ($LASTEXITCODE -ne 0) { throw "Runtime test build failed: $LASTEXITCODE" }
    $suiteArgument = if ($TestSuite -eq 'Quick') { '--quick' } else { '--all' }
    & (Join-Path $projectRoot "build\tests\$Configuration\San9RuntimeTests.exe") $suiteArgument (Join-Path $projectRoot 'assets\config')
    if ($LASTEXITCODE -ne 0) { throw "Offline runtime tests failed: $LASTEXITCODE" }
}
if ($UiSmoke) {
    $companionPath = Join-Path $projectRoot "build\app\$Configuration\San9Companion.exe"
    # A bounded wait: a smoke run that never finishes would otherwise hang the
    # build forever, and the cause is invisible from the build output.
    $smokeProcess = Start-Process -FilePath $companionPath -ArgumentList '--ui-smoke' -WindowStyle Hidden -PassThru
    if (-not $smokeProcess.WaitForExit(120000)) {
        Stop-Process -Id $smokeProcess.Id -Force
        throw "Native GUI smoke checks timed out after 120 seconds"
    }
    if ($smokeProcess.ExitCode -ne 0) { throw "Native GUI smoke checks failed: $($smokeProcess.ExitCode)" }
    Write-Output 'Hidden Win32 dialogs, control bindings and draft isolation passed; game polling and preference writes disabled.'
}
Write-Output "Built San9Companion $Configuration Win32. Live companion monitoring and game have NOT been launched."
