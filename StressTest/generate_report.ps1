param(
    [string]$Csv = "Results/stress_result.csv",
    [string]$Output = "Results/stress_report.html",
    [string]$Title = "Monster Breakers Server Stress Test",
    [double]$MinConnectionRate = 99.0,
    [double]$MinPingResponseRate = 99.0,
    [double]$MaxP95Ms = 100.0,
    [double]$MaxP99Ms = 200.0,
    [string]$CpuSpec = "Intel(R) Core(TM) i5-10400F CPU @ 2.90GHz (2.90 GHz)",
    [string]$RamSpec = "32.0 GB",
    [string]$GpuSpec = "NVIDIA GeForce RTX 3060 (8 GB)"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Csv)) {
    throw "CSV file not found: $Csv"
}

$rows = @(Import-Csv -LiteralPath $Csv)
if ($rows.Count -eq 0) {
    throw "CSV has no result rows: $Csv"
}

$outputPath = [System.IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path -Parent $outputPath
if ($outputDirectory) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$data = foreach ($row in $rows) {
    [ordered]@{
        elapsed = [int]$row.elapsed_sec
        active = [int64]$row.active_clients
        connected = [int64]$row.connected_total
        failed = [int64]$row.connect_failed
        disconnected = [int64]$row.disconnected
        sent = [int64]$row.packets_sent
        received = [int64]$row.packets_received
        bytesSent = [int64]$row.bytes_sent
        bytesReceived = [int64]$row.bytes_received
        sendErrors = [int64]$row.send_errors
        invalid = [int64]$row.invalid_packets
        pings = [int64]$row.pings_sent
        pongs = [int64]$row.pongs_received
        rttAvg = [double]$row.rtt_avg_ms
        rttP50 = [double]$row.rtt_p50_ms
        rttP95 = [double]$row.rtt_p95_ms
        rttP99 = [double]$row.rtt_p99_ms
        rttMax = [double]$row.rtt_max_ms
        cpu = [double]$row.server_cpu_percent
        memory = [double]$row.server_memory_mb
        target = [int64]$row.target_clients
        moveHz = [int]$row.move_hz
        pingHz = [int]$row.ping_hz
        warmup = [int]$row.warmup_sec
        serverPid = [int64]$row.server_pid
    }
}

$json = $data | ConvertTo-Json -Compress
$safeTitle = [System.Net.WebUtility]::HtmlEncode($Title)
$generatedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
$csvHash = (Get-FileHash -LiteralPath $Csv -Algorithm SHA256).Hash
$machineName = [System.Net.WebUtility]::HtmlEncode($env:COMPUTERNAME)
$safeCpuSpec = [System.Net.WebUtility]::HtmlEncode($CpuSpec)
$safeRamSpec = [System.Net.WebUtility]::HtmlEncode($RamSpec)
$safeGpuSpec = [System.Net.WebUtility]::HtmlEncode($GpuSpec)

$html = @"
<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>$safeTitle</title>
<style>
:root{--bg:#08111f;--panel:#101d30;--line:#263750;--text:#edf5ff;--muted:#8ea4c0;--cyan:#38d9ff;--blue:#4c7dff;--green:#4ce0a1;--yellow:#ffd166;--red:#ff647c}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 12% 0,#142a48 0,transparent 35%),var(--bg);color:var(--text);font-family:Segoe UI,Pretendard,Arial,sans-serif}.wrap{max-width:1440px;margin:auto;padding:36px}.top{display:flex;justify-content:space-between;gap:24px;align-items:flex-end;margin-bottom:26px}.eyebrow{color:var(--cyan);font-weight:700;letter-spacing:.18em;font-size:12px}.title{font-size:34px;font-weight:800;margin:6px 0}.subtitle,.meta{color:var(--muted);font-size:14px}.status{display:inline-flex;align-items:center;gap:8px;color:var(--green);font-weight:700;background:#102b29;border:1px solid #205746;border-radius:999px;padding:8px 13px}.dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 12px var(--green)}.cards{display:grid;grid-template-columns:repeat(5,1fr);gap:14px}.card,.chart{background:linear-gradient(145deg,rgba(20,36,58,.96),rgba(12,25,43,.96));border:1px solid var(--line);border-radius:16px;box-shadow:0 18px 50px rgba(0,0,0,.18)}.card{padding:18px}.label{color:var(--muted);font-size:12px;font-weight:650}.value{font-size:29px;font-weight:800;margin-top:8px}.unit{font-size:13px;color:var(--muted);margin-left:4px}.charts{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:14px}.chart{padding:20px;min-height:310px}.chart h2{font-size:16px;margin:0 0 4px}.chart p{font-size:12px;color:var(--muted);margin:0 0 14px}.chart canvas{width:100%;height:225px;display:block}.wide{grid-column:1/-1}.footer{display:flex;justify-content:space-between;color:var(--muted);font-size:12px;margin-top:18px;padding:0 4px}.pass{color:var(--green)}.fail{color:var(--red)}@media(max-width:900px){.cards{grid-template-columns:repeat(2,1fr)}.charts{grid-template-columns:1fr}.wide{grid-column:auto}.top{align-items:flex-start;flex-direction:column}.wrap{padding:20px}}
.cards{grid-template-columns:repeat(6,1fr)}.value{font-size:27px}.evidence{margin-top:14px;padding:20px;display:grid;grid-template-columns:1.2fr 1fr;gap:24px;background:linear-gradient(145deg,rgba(20,36,58,.96),rgba(12,25,43,.96));border:1px solid var(--line);border-radius:16px}.evidence h2{font-size:16px;margin:0 0 12px}.facts{display:grid;grid-template-columns:repeat(2,1fr);gap:8px 18px}.fact{font-size:13px;color:var(--muted)}.fact b{color:var(--text);float:right}.hash{font:11px Consolas,monospace;color:#aabbd0;word-break:break-all;background:#08111f;padding:10px;border-radius:8px}.criteria{font-size:13px;line-height:1.8;color:var(--muted)}@media(max-width:1000px){.cards{grid-template-columns:repeat(3,1fr)}}@media(max-width:700px){.evidence{grid-template-columns:1fr}}
</style>
</head>
<body>
<main class="wrap">
  <header class="top">
    <div><div class="eyebrow">IOCP PERFORMANCE REPORT</div><div class="title">$safeTitle</div><div class="subtitle">다중 가상 클라이언트 로그인 및 실시간 이동 패킷 부하 테스트</div></div>
    <div><div id="status" class="status"><span class="dot"></span><span>TEST COMPLETE</span></div><div class="meta" style="margin-top:8px;text-align:right">Generated $generatedAt</div></div>
  </header>
  <section class="cards">
    <div class="card"><div class="label">최대 동시 접속</div><div class="value" id="peak">-</div></div>
    <div class="card"><div class="label">접속 성공률</div><div class="value" id="success">-</div></div>
    <div class="card"><div class="label">평균 RTT</div><div class="value" id="avgRtt">-</div></div>
    <div class="card"><div class="label">P95 RTT</div><div class="value" id="p95">-</div></div>
    <div class="card"><div class="label">P99 RTT</div><div class="value" id="p99">-</div></div>
    <div class="card"><div class="label">통신 오류</div><div class="value" id="errors">-</div></div>
  </section>
  <section class="charts">
    <article class="chart"><h2>동시 접속 유지</h2><p>시간에 따른 활성 가상 클라이언트 수</p><canvas id="clients"></canvas></article>
    <article class="chart"><h2>패킷 처리량</h2><p>1초당 송신 및 수신 패킷 수</p><canvas id="packets"></canvas></article>
    <article class="chart"><h2>응답 지연시간</h2><p>Ping/Pong 왕복시간의 누적 백분위</p><canvas id="latency"></canvas></article>
    <article class="chart"><h2>서버 자원 사용량</h2><p>서버 프로세스 CPU와 메모리</p><canvas id="resources"></canvas></article>
  </section>
  <section class="evidence"><div><h2>측정 근거</h2><div class="facts"><div class="fact">목표 클라이언트 <b id="target"></b></div><div class="fact">워밍업/측정 <b id="testTime"></b></div><div class="fact">클라이언트당 이동 <b id="moveRate"></b></div><div class="fact">이동 전파 <b>전체 접속자</b></div><div class="fact">RTT 측정 빈도 <b id="pingRate"></b></div><div class="fact">Ping 응답률 <b id="pingSuccess"></b></div><div class="fact">서버 PID <b id="pid"></b></div></div><h2 style="margin-top:18px">테스트 PC 사양</h2><div class="criteria">PC: $machineName<br>CPU: $safeCpuSpec<br>RAM: $safeRamSpec<br>GPU: $safeGpuSpec</div><div class="label" style="margin-top:14px">원본 CSV SHA-256</div><div class="hash">$csvHash</div></div><div><h2>사전 공개 합격 기준</h2><div class="criteria">① 접속 성공률 ≥ $MinConnectionRate%<br>② Ping 응답률 ≥ $MinPingResponseRate%<br>③ P95 RTT ≤ $MaxP95Ms ms<br>④ P99 RTT ≤ $MaxP99Ms ms<br>⑤ 비정상 연결 종료 = 0건<br>⑥ 잘못된 패킷 = 0건</div></div></section>
  <footer class="footer"><span>Monster Breakers · C++ IOCP Server</span><span id="duration"></span></footer>
</main>
<script>
const data=$json;
const last=data[data.length-1];
const peak=Math.max(...data.map(x=>x.active));
const attempts=last.connected+last.failed;
const success=attempts?last.connected/attempts*100:0;
const pingSuccess=last.pings?last.pongs/last.pings*100:0;
const seconds=Math.max(1,last.elapsed);
const totalErrors=last.failed+last.disconnected+last.sendErrors+last.invalid;
const fmt=n=>new Intl.NumberFormat('ko-KR').format(n);
const bytes=n=>n>=1073741824?(n/1073741824).toFixed(2)+' GB':n>=1048576?(n/1048576).toFixed(1)+' MB':(n/1024).toFixed(1)+' KB';
document.querySelector('#peak').innerHTML=fmt(peak)+'<span class="unit">명</span>';
document.querySelector('#success').innerHTML=success.toFixed(1)+'<span class="unit">%</span>';
document.querySelector('#avgRtt').innerHTML=last.rttAvg.toFixed(2)+'<span class="unit">ms</span>';
document.querySelector('#p95').innerHTML=last.rttP95.toFixed(2)+'<span class="unit">ms</span>';
document.querySelector('#p99').innerHTML=last.rttP99.toFixed(2)+'<span class="unit">ms</span>';
document.querySelector('#errors').innerHTML=fmt(totalErrors)+'<span class="unit">건</span>';
document.querySelector('#target').textContent=fmt(last.target)+'명';
document.querySelector('#testTime').textContent=fmt(last.warmup)+'초 / '+fmt(last.elapsed)+'초';
document.querySelector('#moveRate').textContent=last.moveHz+' pkt/s';
document.querySelector('#pingRate').textContent=last.pingHz+'회/s';
document.querySelector('#pingSuccess').textContent=pingSuccess.toFixed(2)+'%';
document.querySelector('#pid').textContent=last.serverPid||'원격/미측정';
document.querySelector('#duration').textContent='측정 시간 '+fmt(last.elapsed)+'초 · 총 수신 '+fmt(last.received)+' packets';
const passed=success>=$MinConnectionRate&&pingSuccess>=$MinPingResponseRate&&last.rttP95<=$MaxP95Ms&&last.rttP99<=$MaxP99Ms&&last.disconnected===0&&last.invalid===0;
const status=document.querySelector('#status');status.querySelector('span:last-child').textContent=passed?'PASS':'FAIL';if(!passed)status.classList.add('fail');

function seriesDelta(key){return data.map((x,i)=>i===0?0:Math.max(0,x[key]-data[i-1][key])/Math.max(1,x.elapsed-data[i-1].elapsed));}
function draw(id,series,yFormat=v=>fmt(Math.round(v))){
 const canvas=document.querySelector(id),dpr=devicePixelRatio||1,w=canvas.clientWidth,h=canvas.clientHeight;
 canvas.width=w*dpr;canvas.height=h*dpr;const c=canvas.getContext('2d');c.scale(dpr,dpr);
 const pad={l:54,r:18,t:14,b:30},pw=w-pad.l-pad.r,ph=h-pad.t-pad.b;
 const values=series.flatMap(s=>s.values),max=Math.max(1,...values)*1.1;
 c.font='11px Segoe UI';c.lineWidth=1;
 for(let i=0;i<=4;i++){const y=pad.t+ph*i/4;c.strokeStyle='#263750';c.beginPath();c.moveTo(pad.l,y);c.lineTo(w-pad.r,y);c.stroke();c.fillStyle='#8ea4c0';c.textAlign='right';c.fillText(yFormat(max*(1-i/4)),pad.l-8,y+4)}
 const step=Math.max(1,Math.ceil(data.length/6));c.textAlign='center';
 data.forEach((x,i)=>{if(i%step&&i!==data.length-1)return;const px=pad.l+(data.length===1?0:i/(data.length-1))*pw;c.fillStyle='#8ea4c0';c.fillText(x.elapsed+'s',px,h-8)});
 series.forEach(s=>{c.strokeStyle=s.color;c.lineWidth=2.5;c.beginPath();s.values.forEach((v,i)=>{const x=pad.l+(data.length===1?0:i/(data.length-1))*pw,y=pad.t+ph-(v/max)*ph;i?c.lineTo(x,y):c.moveTo(x,y)});c.stroke()});
 let lx=pad.l;series.forEach(s=>{c.fillStyle=s.color;c.fillRect(lx,pad.t,12,3);c.fillStyle='#cbd9eb';c.textAlign='left';c.fillText(s.name,lx+18,pad.t+5);lx+=c.measureText(s.name).width+48});
}
function render(){
 draw('#clients',[{name:'활성 클라이언트',color:'#38d9ff',values:data.map(x=>x.active)}]);
 draw('#packets',[{name:'송신 pkt/s',color:'#4c7dff',values:seriesDelta('sent')},{name:'수신 pkt/s',color:'#4ce0a1',values:seriesDelta('received')}]);
 draw('#latency',[{name:'평균',color:'#4ce0a1',values:data.map(x=>x.rttAvg)},{name:'P95',color:'#ffd166',values:data.map(x=>x.rttP95)},{name:'P99',color:'#ff647c',values:data.map(x=>x.rttP99)}],v=>v.toFixed(1)+'ms');
 draw('#resources',[{name:'CPU %',color:'#38d9ff',values:data.map(x=>Math.max(0,x.cpu))},{name:'Memory MB',color:'#4c7dff',values:data.map(x=>Math.max(0,x.memory))}],v=>v.toFixed(0));
}
render();addEventListener('resize',render);
</script>
</body>
</html>
"@

[System.IO.File]::WriteAllText($outputPath, $html, [System.Text.UTF8Encoding]::new($false))
Write-Host "Report created: $outputPath"
