param(
    [int]$Clients = 100,
    [int]$Ramp = 10,
    [int]$Duration = 60,
    [int]$MoveHz = 2,
    [int]$PingHz = 1,
    [string]$HostAddress = "127.0.0.1",
    [int]$Port = 3000,
    [string]$ResultName = "stress",
    [switch]$OpenReport
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$exe = Join-Path $root "x64\Release\StressTest.exe"
$csv = Join-Path $root "Results\${ResultName}_result.csv"
$report = Join-Path $root "Results\${ResultName}_report.html"

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Release executable not found. Build StressTest.sln first: $exe"
}

$serverProcess = Get-Process -Name "Monster_Breakers_Server" -ErrorAction SilentlyContinue | Select-Object -First 1
$arguments = @("--host", $HostAddress, "--port", $Port, "--clients", $Clients, "--ramp", $Ramp,
    "--duration", $Duration, "--move-hz", $MoveHz, "--ping-hz", $PingHz, "--csv", $csv)
if ($serverProcess) {
    $arguments += @("--server-pid", $serverProcess.Id)
    Write-Host "Monitoring local server PID $($serverProcess.Id)"
} else {
    Write-Warning "Local server process was not found. CPU/RAM metrics will be unavailable."
}

& $exe @arguments
$testExitCode = $LASTEXITCODE

& (Join-Path $root "generate_report.ps1") -Csv $csv -Output $report
Write-Host "Portfolio report: $report"

if ($OpenReport) {
    Start-Process $report
}

exit $testExitCode
