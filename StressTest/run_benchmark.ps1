param(
    [int]$Clients = 100,
    [int]$Ramp = 10,
    [int]$Duration = 60,
    [int]$MoveHz = 2,
    [int]$PingHz = 1,
    [int]$Runs = 3,
    [int]$CooldownSeconds = 5,
    [string]$HostAddress = "127.0.0.1",
    [int]$Port = 3000,
    [switch]$OpenReport
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$results = @()

for ($i = 1; $i -le $Runs; $i++) {
    Write-Host "`n===== BENCHMARK RUN $i / $Runs =====" -ForegroundColor Cyan
    & (Join-Path $root "run_test.ps1") -Clients $Clients -Ramp $Ramp -Duration $Duration `
        -MoveHz $MoveHz -PingHz $PingHz -HostAddress $HostAddress -Port $Port -ResultName "run_$i"
    $csv = Join-Path $root "Results\run_${i}_result.csv"
    $last = @(Import-Csv -LiteralPath $csv)[-1]
    $results += [pscustomobject]@{
        Run = $i
        Active = [int]$last.active_clients
        ConnectionRate = if (([int]$last.connected_total + [int]$last.connect_failed) -gt 0) { [int]$last.connected_total * 100.0 / ([int]$last.connected_total + [int]$last.connect_failed) } else { 0 }
        PingRate = if ([int64]$last.pings_sent -gt 0) { [int64]$last.pongs_received * 100.0 / [int64]$last.pings_sent } else { 0 }
        AvgRtt = [double]$last.rtt_avg_ms
        P95 = [double]$last.rtt_p95_ms
        P99 = [double]$last.rtt_p99_ms
        Cpu = [double]$last.server_cpu_percent
        Memory = [double]$last.server_memory_mb
        Errors = [int64]$last.connect_failed + [int64]$last.disconnected + [int64]$last.send_errors + [int64]$last.invalid_packets
        Hash = (Get-FileHash -LiteralPath $csv -Algorithm SHA256).Hash
    }
    if ($i -lt $Runs) { Start-Sleep -Seconds $CooldownSeconds }
}

$reportPath = Join-Path $root "Results\benchmark_comparison.html"
$rows = ($results | ForEach-Object {
    "<tr><td>Run $($_.Run)</td><td>$($_.Active)</td><td>$('{0:F2}' -f $_.ConnectionRate)%</td><td>$('{0:F2}' -f $_.PingRate)%</td><td>$('{0:F2}' -f $_.AvgRtt)</td><td>$('{0:F2}' -f $_.P95)</td><td>$('{0:F2}' -f $_.P99)</td><td>$('{0:F2}' -f $_.Cpu)%</td><td>$('{0:F1}' -f $_.Memory) MB</td><td>$($_.Errors)</td></tr>"
}) -join "`n"
$hashes = ($results | ForEach-Object { "<div><b>Run $($_.Run)</b> $($_.Hash)</div>" }) -join "`n"
$avgP95 = ($results | Measure-Object P95 -Average).Average
$maxP95 = ($results | Measure-Object P95 -Maximum).Maximum
$avgCpu = ($results | Measure-Object Cpu -Average).Average
$maxMemory = ($results | Measure-Object Memory -Maximum).Maximum
$allPassed = (@($results | Where-Object { $_.ConnectionRate -lt 99 -or $_.PingRate -lt 99 -or $_.P95 -gt 100 -or $_.Errors -gt 0 }).Count -eq 0)
$verdict = if ($allPassed) { "PASS" } else { "FAIL" }
$verdictClass = if ($allPassed) { "pass" } else { "fail" }
$generated = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

$html = @"
<!doctype html><html lang="ko"><head><meta charset="utf-8"><title>Monster Breakers 3-Run Benchmark</title><style>
body{margin:0;background:#08111f;color:#edf5ff;font-family:Segoe UI,Pretendard,sans-serif}.wrap{max-width:1400px;margin:auto;padding:40px}.top{display:flex;justify-content:space-between;align-items:center}.sub{color:#8ea4c0}.badge{padding:10px 18px;border-radius:999px;font-weight:800}.pass{background:#103128;color:#4ce0a1}.fail{background:#351822;color:#ff647c}.cards{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin:26px 0}.card,.panel{background:#101d30;border:1px solid #263750;border-radius:16px;padding:20px}.label{font-size:12px;color:#8ea4c0}.value{font-size:28px;font-weight:800;margin-top:8px}table{width:100%;border-collapse:collapse;font-size:13px}th,td{padding:13px 10px;text-align:right;border-bottom:1px solid #263750}th{color:#8ea4c0}th:first-child,td:first-child{text-align:left}.hash{font:11px Consolas,monospace;color:#8ea4c0;word-break:break-all;line-height:1.8}.foot{color:#8ea4c0;font-size:12px;margin-top:16px}</style></head><body><main class="wrap">
<header class="top"><div><div class="sub">REPEATED PERFORMANCE VERIFICATION</div><h1>Monster Breakers Server · $Runs-Run Benchmark</h1><div class="sub">동일 조건 반복 측정 · $Clients clients · $Duration sec · move $MoveHz/s · ping $PingHz/s</div></div><div class="badge $verdictClass">$verdict</div></header>
<section class="cards"><div class="card"><div class="label">평균 P95 RTT</div><div class="value">$('{0:F2}' -f $avgP95) ms</div></div><div class="card"><div class="label">최대 P95 RTT</div><div class="value">$('{0:F2}' -f $maxP95) ms</div></div><div class="card"><div class="label">평균 서버 CPU</div><div class="value">$('{0:F2}' -f $avgCpu)%</div></div><div class="card"><div class="label">최대 서버 메모리</div><div class="value">$('{0:F1}' -f $maxMemory) MB</div></div></section>
<section class="panel"><table><thead><tr><th>반복</th><th>유지 인원</th><th>접속률</th><th>Ping 응답률</th><th>평균 RTT</th><th>P95</th><th>P99</th><th>CPU</th><th>메모리</th><th>오류</th></tr></thead><tbody>$rows</tbody></table></section>
<section class="panel" style="margin-top:14px"><div class="label">원본 CSV SHA-256</div><div class="hash">$hashes</div></section><div class="foot">Generated $generated · 합격 기준: 접속률/Ping 응답률 99% 이상, P95 100ms 이하, 오류 0건</div>
</main></body></html>
"@
[System.IO.File]::WriteAllText($reportPath, $html, [System.Text.UTF8Encoding]::new($false))
Write-Host "Comparison report: $reportPath" -ForegroundColor Green
if ($OpenReport) { Start-Process $reportPath }
