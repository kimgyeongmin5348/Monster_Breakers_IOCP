# Monster Breakers 서버 스트레스 테스트

실제 클라이언트와 같은 로그인/이동 패킷을 보내는 IOCP 기반 가상 클라이언트입니다. 테스트 결과는 1초 단위 CSV로 저장됩니다.

## 빌드

Visual Studio에서 `StressTest.sln`을 열어 `Release | x64`로 빌드합니다.

## 실행 순서

1. `Monster_Breakers_Server.exe`를 먼저 실행합니다.
2. StressTest 폴더에서 아래 명령을 실행합니다.

```powershell
.\run_test.ps1 -Clients 100 -Ramp 10 -Warmup 30 -Duration 60 -MoveHz 2 -PingHz 1 -OpenReport
```

테스트가 끝나면 다음 결과물이 자동 생성됩니다.

- `Results/stress_result.csv`: 1초 단위 원본 측정값
- `Results/stress_report.html`: 포트폴리오 캡처용 대시보드

HTML 대시보드는 외부 라이브러리나 인터넷 연결 없이 동작하며 최대 동시 접속, 접속 성공률, 평균 패킷 처리량, 누적 트래픽, 오류 수와 시간별 그래프를 한 화면에 표시합니다.

가상 클라이언트는 로그인 직후 실제 클라이언트와 동일하게 `CS_P_LOADING_DONE`을 전송하므로 이동 패킷이 서버 게임 로직에서 실제 처리됩니다. `Warmup` 시간 동안에도 같은 이동 부하를 발생시키지만 해당 구간의 RTT는 최종 통계에서 제외합니다. 워밍업 중 발생한 접속 종료와 오류는 제외하지 않습니다.

서버는 처리한 이동 패킷을 송신자를 제외한 전체 접속자에게 전파합니다. 스트레스 클라이언트의 패킷 발생 시점은 실제 사용자처럼 측정 구간 전체에 균등하게 분산하지만, 전체 패킷 발생량과 동기화 대상 수는 줄이지 않습니다.

보고서에는 실제 Ping/Pong 왕복시간의 평균·P50·P95·P99, Ping 응답률, 서버 프로세스 CPU·메모리, 테스트 조건과 원본 CSV의 SHA-256도 포함됩니다. 로컬 서버 프로세스는 `run_test.ps1`이 자동으로 찾아 측정합니다.

현재 보고서의 테스트 PC 사양은 다음과 같이 설정되어 있습니다: Intel Core i5-10400F, RAM 32 GB, NVIDIA GeForce RTX 3060 8 GB. 다른 PC에서 측정할 때는 `generate_report.ps1`의 `CpuSpec`, `RamSpec`, `GpuSpec` 매개변수를 변경하세요.

레이아웃을 먼저 확인하려면 `Results/sample_result.csv`로 샘플 보고서를 생성할 수 있습니다. 이 데이터는 화면 확인용 예시이며 실제 성능 측정 결과가 아닙니다.

주요 옵션:

- `--host 127.0.0.1`: 서버 IPv4 주소
- `--port 3000`: 서버 포트
- `--clients 100`: 가상 클라이언트 수
- `--ramp 10`: 전체 접속을 분산할 시간(초)
- `--duration 60`: 모든 접속 완료 후 부하 유지 시간(초)
- `--move-hz 2`: 클라이언트 한 명당 초당 이동 패킷 수
- `--csv Results/stress_result.csv`: 결과 파일 경로

기존 CSV로 보고서만 다시 만들려면 다음 명령을 사용합니다.

```powershell
.\generate_report.ps1 -Csv .\Results\stress_result.csv -Output .\Results\stress_report.html
```

처음에는 100명으로 확인하고 250명, 500명, 1,000명 순으로 늘리는 것을 권장합니다. 동일 PC에서 서버와 테스트 도구를 함께 실행하면 측정값에 양쪽 프로그램의 부하가 모두 포함됩니다. 최종 포트폴리오 측정은 가능하면 별도 PC에서 실행하세요.

## 포트폴리오용 3회 반복 측정

단일 실행의 우연한 결과를 피하려면 같은 조건으로 3회 반복하고 비교 보고서를 사용하세요.

```powershell
.\run_benchmark.ps1 -Clients 500 -Ramp 30 -Warmup 30 -Duration 600 -MoveHz 2 -PingHz 1 -Runs 3 -OpenReport
```

각 실행의 상세 보고서와 `Results/benchmark_comparison.html`이 생성됩니다. 비교 보고서는 실행별 접속률, Ping 응답률, RTT, CPU, 메모리, 오류와 각 원본 CSV 해시를 함께 표시합니다.

기본 PASS 기준은 접속 성공률 99% 이상, Ping 응답률 99% 이상, P95 RTT 100ms 이하, P99 RTT 200ms 이하, 비정상 종료 및 잘못된 패킷 0건입니다. 포트폴리오에는 테스트 PC 사양, 서버와 부하 발생기의 실행 위치, Release x64 빌드 사용 여부를 함께 기재하세요.

테스트는 본인이 관리하거나 허가받은 서버에서만 실행하세요.
