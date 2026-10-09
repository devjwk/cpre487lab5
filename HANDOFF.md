# Lab 5 인수인계 (2026-10-09 기준)

다른 세션에서 이 문서만 읽고 이어서 진행할 수 있도록 정리한 것이다. 숫자는 전부 저장소의 리포트·로그에서 온 것이고, "예상"이라고 적은 것은 아직 측정하지 않은 값이다.

- 팀 06: Jongwoo Kim, Zach Dixon. 저장소 `devjwk/cpre487lab5` (private), 브랜치 `main`.
- 핸드아웃: `CprE487_587_Lab5.pdf`. **노란색 강조 문장이 제출 항목**이다.
- 실험실 컴퓨터 작업 폴더: `/home/jwk0425/jwk_personal_lab/lab5`
- Mac 작업 폴더: `~/School/CprE487/cpre487lab5`

## 1. 한눈에 보는 상태

| 핸드아웃 | 내용 | 상태 |
|---|---|---|
| 3.1, 3.2 | `computeAccelerated` | 완료 (Mac·보드 검증) |
| 3.3 | MAC 6개 (staged·piped × 8/4/2-bit), XSA, 리포트 | 완료 |
| 3.4 | 6개 MAC으로 보드에서 전체 inference | 완료 (전 레이어 일치) |
| 4.1–4.6 | PPA 측정과 보고서 글 | 측정 완료, 글 초안 완료 |
| 5.1 | 방식 선택 | **spatial accumulation** 으로 확정 |
| 5.3 | 레이어별 비트 폭과 이유 | 완료 (Mac) |
| 5.4 | 가변 MAC 구현 | VHDL·소프트웨어 완료, **시뮬레이션 통과**, Vivado·보드는 미실행 |
| 5.5 | 8/4/2-bit 대비 비교 | **남음** (실험실 측정 필요) |
| 6 | 공유 스프레드시트 | **남음** |
| 8 | 보고서 PDF, 제출 zip | **남음** |

## 2. 지금 바로 할 일: 실험실 작업

실험실에서만 할 수 있는 것은 아래 다섯 단계다. 순서대로 하고, 각 단계의 "확인"을 통과한 뒤 다음으로 간다. **아래 스크립트 중 가변 MAC 부분은 Vivado/Vitis에서 한 번도 실행해 본 적이 없다.** 에러가 나면 로그의 `ERROR` 줄부터 본다.

### 0) 준비

```bash
cd /home/jwk0425/jwk_personal_lab/lab5
git pull
source /remote/Xilinx/2020.1/Vivado/2020.1/settings64.sh
```

`git pull`을 하면 `sw/framework/workspace_file_transfer` 안의 파일 중 git에 올라가 있던 것만 지워진다 (실수로 올라갔던 Vitis 작업 폴더를 저장소에서 뺐기 때문). **폴더 자체와 git이 추적하지 않던 파일은 남아서 불완전한 상태가 된다.** `file_transfer_vitis`는 폴더가 있으면 다시 만들지 않으므로, 3단계 전에 이 폴더를 치워야 한다:

```bash
cd /home/jwk0425/jwk_personal_lab/lab5/sw/framework
mv workspace_file_transfer workspace_file_transfer.broken
```

그 뒤 `./scripts/file_transfer_vitis`를 실행하면 새로 만든다.

### 1) 가변 MAC 단독 합성

```bash
cd /home/jwk0425/jwk_personal_lab/lab5/hw/variable_mac/vivado
vivado -mode batch -source run_synth.tcl > synth.log 2>&1
grep -n '^ERROR' synth.log | head
grep -A2 'WNS(ns)' timing_summary.rpt | sed -n 3p
grep 'Slice LUTs\|Slice Registers  \|DSPs  ' utilization.rpt
grep 'Total On-Chip\|Dynamic (W)\|Device Static' power.rpt
```

확인:
- `timing_summary.rpt`, `utilization.rpt`, `power.rpt` 세 파일이 생긴다.
- WNS(첫 번째 숫자). 제약은 200 MHz(5 ns)다. **음수여도 실패가 아니다.** 시스템은 100 MHz로 돌기 때문에 `Fmax = 1 / (5 ns − WNS)`가 100 MHz보다 크면 된다. 음수면 그 사실을 보고서에 적는다.
- 고정 폭 MAC과의 비교 기준: staged 8-bit는 LUT 118 / FF 41 / DSP 0, piped 8-bit는 LUT 41 / FF 138 / DSP 1.

### 2) 가변 MAC의 XSA

```bash
cd /home/jwk0425/jwk_personal_lab/lab5/hw/xsa
vivado -mode batch -source ../build_xsa.tcl -tclargs ../simple_interface/vivado/simple_interface/simple_interface.xpr variable > variable.log 2>&1
ls -la variable_mac.xsa
grep -n '^ERROR' variable.log | head
grep -A2 'WNS(ns)' variable_mac_system_timing.rpt | sed -n 3p
unzip -p variable_mac.xsa '*.hwh' | grep -o 'MODTYPE="[a-z]*_mac"\|NAME="M_TDATA_NUM_BYTES" VALUE="[0-9]*"' | sort -u
grep '_mac_0 ' variable_mac_system_utilization.rpt
```

확인:
- `variable_mac.xsa`가 생기고 `ERROR`가 없다.
- 시스템 WNS가 양수 (100 MHz).
- XSA 안에 `MODTYPE="variable_mac"`, `M_TDATA_NUM_BYTES` 값이 `4` (MAC에 32비트 단어가 그대로 들어가야 한다).

`hw/simple_interface/`는 Lab 3 블록 디자인 프로젝트의 복사본이고 git에는 없다 (`.gitignore`). 실험실 컴퓨터에는 남아 있고, 마지막 상태는 piped 2-bit다. 스크립트가 MAC 셀을 가변 MAC으로 바꿔 끼운다. 이 폴더가 없으면 다시 복사한다:
`mkdir -p hw/simple_interface/vivado && cp -r /home/jwk0425/jwk_personal_lab/lab3/simple_interface/vivado/simple_interface hw/simple_interface/vivado/`

막힐 가능성이 큰 곳: 블록 디자인에서 MAC 교체 후 연결(`connect_bd_intf_net`) 또는 `validate_bd_design`. 가변 MAC에는 `SD_AXIS_TUSER` 포트가 없다 (원래도 연결돼 있지 않았다).

### 3) SD카드에 가변 모델 올리기

터미널 A (전송 서버, 끝날 때까지 켜 둔다):

```bash
cd /home/jwk0425/jwk_personal_lab/lab5/sw/framework
./scripts/file_transfer_vitis
```

`HTTP file server started!`가 나오면 터미널 B에서:

```bash
cd /home/jwk0425/jwk_personal_lab/lab5/sw/framework
curl -s 192.168.1.2/data/model/qvar/ -X POST -H Expect:
find data/model/qvar -type f -exec sh -c 'echo {} && curl -s 192.168.1.2/{} -X POST --data-binary "@{}" -H Expect:' \;
curl 192.168.1.2/data/model/qvar
```

확인: 마지막 줄에 `quant_params.txt` 포함 17개 파일. 끝나면 터미널 A에서 Ctrl-C.

주의:
- **`./scripts/upload_data`는 쓰지 않는다.** 첫 줄이 SD카드를 포맷한다. 위처럼 직접 올리면 기존 파일(q8, q4, q2, 이미지)이 그대로 남는다.
- 없는 하위 경로를 조회하면 `ERROR: f_stat failed`가 나오는데 카드 고장이 아니다. 빈 카드에서 `curl 192.168.1.2/`는 아무것도 출력하지 않는 것이 정상이다.

### 4) 가변 MAC 보드 실행

```bash
cd /home/jwk0425/jwk_personal_lab/lab5/sw/framework
source /remote/Xilinx/2020.1/Vitis/2020.1/settings64.sh
./scripts/create_vitis -xsa_path ../../hw/xsa/variable_mac.xsa -variable yes -workspace_dir workspace/variable > create_variable.log 2>&1
grep -n 'error' create_variable.log | head
timeout 240 ./scripts/flash_vitis -workspace_dir workspace/variable > ../../util/board_logs/variable.txt 2>&1
tr -d '\r' < ../../util/board_logs/variable.txt | grep -E 'operand bits|self-test|Layer [0-9]+ .*MATCH|^Total|EXCEPTION|COMPLETE'
```

확인 (전부 만족해야 통과):
- `operand bits per conv/dense layer: 8 4 8 4 4 4 4 8`
- `MAC self-test, 8-bit ... PASS`, `4-bit ... PASS`, `2-bit ... PASS` 세 줄
- `Layer N ...: MATCH` 13줄
- `Total: 131595776 MAC ops in 38244352 words and 310728 packets -> ALL LAYERS MATCH`
- `runTests() COMPLETE`, `EXCEPTION` 없음

`MAC unit X ms`의 합이 §5.5의 핵심 측정값이다. 예상은 약 8.6 s (고정 폭은 29.07 s). 예상 근거는 "시간이 FIFO 단어 수에 비례한다"는 어제의 측정이고, 실제 값은 다를 수 있다.

self-test가 FAIL이면 하드웨어와 소프트웨어의 단어 형식이 어긋난 것이다. Mac에서 `hw/variable_mac/tb/run_sim.sh`로 재현되는지부터 본다 (같은 C++ 코드로 만든 패킷을 쓴다).

### 5) (권장) 고정 폭 6개 MAC의 전수 검사

어제의 보드 검증은 모델이 쓰는 값만 MAC에 넣었다. 2-bit는 −1, 0, 1만 나와서 약했다. 새로 넣은 self-test를 6개 MAC에도 한 번씩 돌린다. 기존 로그(`staged8.txt` 등, 보고서 표 2의 근거)는 덮어쓰지 않도록 다른 이름으로 저장한다.

```bash
cd /home/jwk0425/jwk_personal_lab/lab5/sw/framework
for v in "staged 8" "staged 4" "staged 2" "piped 8" "piped 4" "piped 2"; do
  set -- $v; n=$1$2
  echo y | ./scripts/create_vitis -xsa_path ../../hw/xsa/${1}_mac_${2}bit.xsa -quant_bits $2 -workspace_dir workspace/$n > create_$n.log 2>&1
  timeout 240 ./scripts/flash_vitis -workspace_dir workspace/$n > ../../util/board_logs/selftest_$n.txt 2>&1
  echo "$n: $(tr -d '\r' < ../../util/board_logs/selftest_$n.txt | grep -E 'self-test|^Total' | tr '\n' ' ')"
done
```

확인: 각 줄에 `PASS`와 `ALL LAYERS MATCH`. (`echo y`는 이미 있는 작업 폴더를 지우고 다시 만들겠느냐는 질문에 답하는 것이다.)

### 6) 결과 올리기

```bash
cd /home/jwk0425/jwk_personal_lab/lab5
git add hw/variable_mac/vivado/*.rpt hw/xsa/variable_mac* util/board_logs
git status --short
git commit -m "Lab 5 Section 5: variable MAC reports, XSA and board runs"
git push origin main
```

**`git add -A`나 `git add .`는 쓰지 않는다.** Vitis 작업 폴더가 딸려 올라간다.

로그아웃 전에 `/tmp/digilent*`를 지우라는 수업 공지가 있었다.

## 3. 실험실 작업 뒤에 Mac에서 할 일

1. **노트북 확장** (`util/lab5_06.ipynb`, 생성기 `tools/make_notebook.py`): 가변 MAC 행 추가. 리포트 파일 이름이 고정 폭과 다르다 (`hw/variable_mac/vivado/{timing_summary,utilization,power}.rpt`, 시스템은 `hw/xsa/variable_mac_system_*.rpt`, 보드 로그는 `util/board_logs/variable.txt`).
2. **§5.5 비교표**: 가변 vs 8/4/2-bit 고정. 아래 4절의 "두 가지 양자화 방식" 주의사항을 반드시 지킨다.
3. **보고서 4절·5절 작성** (`lab5_report_06.docx`, 생성기 `tools/make_report.js`).
4. **스프레드시트** `Demo_Lab3_4_5_Design_Points` (Teams lectures 채널 공유 파일). 열: Team, Frequency, LUT/BRAM/DSP Utilization, Ops (MACs) per Cycle, Single Inference Performance, Inference Throughput, Average Power, Energy per Inference, Worst-case Quantization Error, Accuracy, Description. 데모 전날 저녁까지.
5. **노트북 실행 결과 저장**: 지금 `.ipynb`에는 출력이 없다 (Mac에 Jupyter가 없어서 셀 코드만 실행해 확인했다). 제출 전에 Jupyter에서 한 번 실행해 저장한다.
6. **제출 패키징**: `lab5_report_06.pdf`, `lab5_src_06.zip`. zip 구조는 핸드아웃 8.2:
   `util/lab5_06.ipynb`(+도우미 스크립트), `sw/{8bit,4bit,2bit,variable}_integrated_framework`, `hw/{8bit,4bit,2bit,variable}_mac`. 각 프레임워크 폴더는 `Config.h`에서 폭을 고정해 두고, TA가 create/flash 스크립트만 돌리면 되게 한다. Lab 4의 `lab4_src_06.zip`이 같은 방식이었다.

## 4. 알아야 할 결정과 사실

### 측정 결과 (고정 폭, 완료)

MAC 단독, 200 MHz 제약:

| MAC | 폭 | LUT | FF | DSP | WNS (ns) | Fmax (MHz) | 동적 (W) | 정적 (W) |
|---|---|---|---|---|---|---|---|---|
| staged | 8 | 118 | 41 | 0 | 0.834 | 240.0 | 0.025 | 0.105 |
| staged | 4 | 76 | 41 | 0 | 1.100 | 256.4 | 0.023 | 0.105 |
| staged | 2 | 53 | 41 | 0 | 1.283 | 269.0 | 0.023 | 0.105 |
| piped | 8 | 41 | 138 | 1 | 0.511 | 222.8 | 0.016 | 0.105 |
| piped | 4 | 49 | 122 | 1 | 0.578 | 226.1 | 0.015 | 0.105 |
| piped | 2 | 56 | 114 | 1 | 0.721 | 233.7 | 0.015 | 0.105 |

보드, image 0 한 장 (`util/board_logs`):

| | 소프트웨어 (ARM) | MAC 경유 |
|---|---|---|
| staged 8 / 4 / 2 | 1,353 / 1,353 / 1,348 ms | 29,073 / 29,080 / 28,303 ms |
| piped 8 / 4 / 2 | 1,353 / 1,353 / 1,348 ms | 29,074 / 29,080 / 28,303 ms |

- 1회 inference = 131,595,776 MAC, 310,728 그룹.
- 병목은 ARM이 FIFO에 단어를 하나씩 쓰는 것이다. 단어 하나에 약 0.215 µs, 패킷 하나에 약 1 µs.
- 시스템 전력 1.683 W 중 PS7이 1.529 W, FIFO 8~9 mW, MAC 2 mW 이하. FIFO는 LUT 764~777.
- 시스템 안에서 MAC의 `SD_AXIS_TUSER`(bias 로드)는 연결돼 있지 않다 (XSA로 확인). bias와 zero point 보정은 소프트웨어에서 더한다.
- **미해결**: 2-bit 빌드가 2.7% 빠른 원인. MAC과 무관하고(staged = piped), 컴파일된 반복문의 명령어 수도 같다는 것까지만 확인했다. 보고서에는 확인된 사실과 추측을 구분해 적어 두었다.

### §5 설계

**방식**: spatial accumulation. temporal은 다른 팀(Miller)이 Teams에서 먼저 선언했다.

**고른 이유**: 실행 시간이 FIFO 단어 수로 정해지므로, 한 단어에 여러 쌍을 담아 단어 수를 줄이는 방식만 보드에서 측정 가능한 이득을 준다.

**단어 형식** (`sw/framework/src/Mac.h`, `hw/variable_mac/hdl/variable_mac.vhd` 주석에도 있음):
- 패킷의 첫 단어는 헤더. 비트 [1:0] = 0 / 1 / 2 → 8 / 4 / 2-bit.
- 데이터 단어 = 상위 16비트 가중치, 하위 16비트 activation. 각각 `16 / bits`개 필드로 나누고, 같은 번호의 필드끼리 곱해 모두 더한다. 8-bit 2쌍, 4-bit 4쌍, 2-bit 8쌍.
- 패킷당 데이터 단어는 최대 1024개 (FIFO 깊이 2048).
- `TLAST`에서 32비트 합을 내보내고 누산기를 비운다.

**하드웨어 구조**: 2×2 곱셈기 16개짜리 유닛 2개. 8-bit에서는 시프트해서 더해 8×8 곱 하나, 4-bit에서는 4×4 곱 두 개, 2-bit에서는 대각선의 4개만 쓴다. 3단 파이프라인(입력 → 곱 → 누산·출력).

**레이어별 폭** (§5.3의 답):

| conv1 | conv2 | conv3 | conv4 | conv5 | conv6 | dense1 | dense2 |
|---|---|---|---|---|---|---|---|
| 8 | 4 | 8 | 4 | 4 | 4 | 4 | 8 |

고른 방법: 전부 8-bit에서 시작해, top-1과 top-10이 1%p 이내로 유지되는 동안 FIFO 단어를 가장 많이 줄이는 레이어부터 한 단계씩 내렸다. 2-bit는 어느 레이어든 3%p 이상 떨어져서 쓰지 않았다 (MAC은 2-bit도 지원한다).

결과: FIFO 데이터 단어 38,244,352개 (고정 폭 131,595,776의 29%), 패킷 310,728개. 정확도는 validation 1000장에서 top-1 23.5%, top-10 60.6% (고정 8-bit는 23.8 / 60.4).

### 두 가지 양자화 방식 (보고서에서 섞지 말 것)

| | Lab 4 방식 | calibrated 방식 |
|---|---|---|
| 쓰는 곳 | §3·§4의 8/4/2-bit 고정 폭 (`data/model/q8,q4,q2`) | §5의 가변 폭 (`data/model/qvar`) |
| 규칙 | Lab 4 핸드아웃 공식: `Si = qmax / max\|Ix − avg\|`, `zi = −round(avg·Si)` | zero point = 가장 낮은 코드(전체 범위 사용), clip은 양자화 오차가 최소인 percentile |
| 전체 8 / 4 / 2-bit top-1 (800장) | 23.9 / 6.6 / 0.1 % | 24.1 / 21.6 / 3.4 % |

- Lab 4 제출물은 핸드아웃이 지정한 공식을 따른 것이라 문제가 아니다.
- calibrated 방식의 범위는 validation 마지막 200장으로 정했고, 800장 수치는 나머지 앞 800장 기준이다. 1000장 수치(23.5 / 60.6)에는 그 200장이 포함된다. 보고서에 이 점을 적는다.
- **§5.5에서 "방식이 달라서 좋아진 것"과 "가변 폭이라서 좋아진 것"을 구분해야 한다.** 같은 calibrated 방식의 고정 8/4/2-bit 수치를 나란히 놓는다 (`python3 util/mixed_precision_study.py`가 출력한다).
- 800장에서 1.5%p 이내의 차이는 측정 오차 수준이다.

## 5. 파일 지도

| 경로 | 내용 |
|---|---|
| `sw/framework/src/Mac.h` | MAC 인터페이스. 고정 폭과 가변 폭(`QUANT_VARIABLE`) 두 경로, 소프트웨어 모델, `macSelfTest` |
| `sw/framework/src/Quant.h` | 양자화 파라미터 (`bits`, `outBits`), `quantize(x, scale, zero, bits)` |
| `sw/framework/src/ML.cpp` | 모델 구성, 테스트, `runAcceleratedCheck`(레이어별 일치·시간·단어 수) |
| `sw/framework/scripts/create_vitis` | `-quant_bits <8\|4\|2>` 또는 `-variable yes` |
| `hw/{staged,piped}_mac/vivado/run_synth.tcl` | `-tclargs <폭>`, 리포트 `*_<N>bit.rpt` |
| `hw/variable_mac/` | 가변 MAC VHDL, 테스트벤치(`tb/run_sim.sh`), 합성 스크립트 |
| `hw/build_xsa.tcl` | `<xpr> <staged\|piped> <폭>` 또는 `<xpr> variable` |
| `hw/xsa/` | XSA와 시스템 리포트 |
| `util/lab5_06.ipynb` | PPA 계산 (리포트와 보드 로그를 직접 읽음) |
| `util/mixed_precision_study.py` | §5.3 분석. `export b1 … b8`로 `qvar` 생성 |
| `util/board_logs/` | 보드 실행 출력 |
| `tools/` | 노트북·보고서 생성기 (제출물 아님) |
| `lab5_report_06.docx` | 보고서 초안. git에는 없고 Mac 로컬에만 있다 |

### Mac에서 확인하는 방법

```bash
cd sw/framework
make clean && make && ./build/ml                      # 8-bit 고정 (QUANT_BITS=4, 2도 가능)
make clean && make QUANT_VARIABLE=1 && ./build/ml     # 가변 폭
./build/ml val 1000                                   # 정확도. 뒤에 mac을 붙이면 MAC 경로
cd ../../hw/variable_mac/tb && ./run_sim.sh           # 가변 MAC 시뮬레이션 (nvc 필요: brew install nvc)
```

기대값: 모든 빌드에서 `MAC self-test ... PASS`, `ALL LAYERS MATCH`. 고정 8-bit val 1000장 23.8 / 60.4, 가변 23.5 / 60.6. 시뮬레이션은 `132432 packets checked, 0 errors`, `TEST PASSED`.

폭을 바꿔 빌드할 때는 `make clean`을 먼저 한다.

## 6. 작업 규칙

- 커밋 메시지에 Claude 공동 작성자 줄을 넣지 않는다.
- 보고서는 Word로, **핸드아웃의 강조 항목만** 쓴다. 서술은 Lab 1 보고서 방식: 절 제목에 핸드아웃 번호, 굵은 질문 뒤에 답, 쉬운 영어, 표 캡션 "Table N."
- 실험실 작업은 사용자가 명령을 실행해 출력을 붙여 주고, 그 출력으로 통과 여부를 판정하는 방식으로 한다. 전체 로그 대신 `grep`으로 필요한 줄만 뽑게 한다.
- 측정하지 않은 값은 "예상"으로 표시하고, 확인하지 못한 원인은 그렇게 적는다.
