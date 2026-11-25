# ESP32·e-Ink 하드웨어 및 소프트웨어 버전 기준

2026-11-25 정리. 2조 위험구역 작업자 추적 관제실. **실제 기판에서 확인한 정보, 빌드가 대상으로 하는 모델, 미확인 항목을 구분한다.** 모형 제작 기준은 [v6](foam-v6.md)(55×40 cm)이다.

## 1. 하드웨어 식별

| 구분 | 모델·규격 | 확인 상태 |
|---|---|---|
| 표시 제어 기판 | Good Display(GooDisplay) **ESP32-L** | 실물 사진에서 기판 표기 확인 |
| ESP32 모듈 | ESP32-S 계열로 기존 README에 기록 | 정확한 모듈 표기/PCB 리비전은 실물 재대조 필요 |
| ESP32 칩 | 기존 진행현황에는 `ESP32-D0WD rev.1.1`, ESP32 README의 esptool 기록에는 `ESP32-D0WD-V3` | 기록이 다르므로 현재 지급·교체 기판의 정확한 칩명·실리콘 리비전은 미확정. 동일 기판이라고 단정하지 않는다. |
| USB-시리얼 | CH340; 실습 PC에서 COM4 | 기존 접속·전송 기록. COM4는 PC 및 연결에 따라 달라짐. |
| e-Ink 어댑터 | Good Display **DESPI-C02** | 실물 사진에서 확인. P2는 ESP32-L P5, P1은 패널 FPC 연결. |
| 리본 인쇄 | **FPC-C001** | 실물 사진의 인쇄. 패널 모델 번호가 아니다. |
| 펌웨어 대상 패널 | **GDEY075Z08** (7.5인치, 800×480, 흑·백·적 BWR) | 제조사 전용 예제 및 프로젝트 빌드 대상 확인. 실제 지급 패널의 후면 모델 라벨은 아직 대조해야 함. |
| 패널 구동 저항 | DESPI-C02 RESE **0.47 Ω** | GDEY075Z08임을 확인한 경우에 적용. 실제 패널이 다른 모델이면 해당 모델 설정/드라이버를 확인한다. |

근거: [실물 연결 사진](esp32-connectors.png), [하드웨어 결선](하드웨어_연결.md), [ESP32 README](../esp32/README.md), [기존 시험 기록](프로젝트_진행현황.md), [Good Display ESP32-L](https://www.good-display.com/product/338.html), [DESPI-C02](https://www.good-display.com/product/516.html), [GDEY075Z08](https://www.good-display.com/product/394.html).

## 2. 재현 가능한 개발 환경

| 항목 | 사용 기준 |
|---|---|
| Arduino IDE | **2.3.11** (기존 실습 PC 기록; 2.x에서 준비된 스케치 사용) |
| Arduino-ESP32 보드 패키지 | **esp32 by Espressif Systems 2.0.17** |
| IDE 보드 선택 | **ESP32 Dev Module** (classic ESP32) |
| CLI FQBN | `esp32:esp32:esp32` |
| Arduino CLI | 사용 이력 있음; 실행 파일의 정확한 버전은 저장소에서 확인되지 않아 현장 기록 필요 |
| 제조사 라이브러리 | **ESP32epdx 1.0.9** — 고정 커밋의 library.properties에서 확인 |
| 제조사 원본 커밋 | `d5ed3b777bedb9b407ccff09dc810a9248eb7926` |
| 전용 예제 | `examples/3-Colors (BWR)/7.5/GDEY075Z08/EPD.cpp`, `EPD.h` |
| 프로젝트 소스 | `esp32/ControlRoom`, 준비·빌드 스크립트 `esp32/build.ps1` |
| STM32 참고 | STM32F103RBT6 / STM32F10x Firmware Library V1.0(SPL) / IAR EWARM의 EMB_FLASH. IAR 정확한 설치 버전은 미기록. |

[고정 라이브러리 메타데이터](https://github.com/gooddisplayshare/ESP32epdx/blob/d5ed3b777bedb9b407ccff09dc810a9248eb7926/library.properties) · [고정 패널 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/3-Colors%20%28BWR%29/7.5/GDEY075Z08) · [Espressif 2.0.17](https://github.com/espressif/arduino-esp32/releases/tag/2.0.17) · [프로젝트 빌드 스크립트](../esp32/build.ps1)

빌드 스크립트가 위 커밋의 라이브러리, EPD 전용 소스, SPI/GUI 및 글꼴을 받아 통합한다. 다른 예제/최신 라이브러리와 임의로 섞지 않는다. Arduino IDE용 스케치에 포함되는 코드는 원본 그대로가 아니라 프로젝트의 통합 패치가 적용된 코드이다. `PartImage`는 전체 48,000바이트 크기로 확보하며, 흑백·빨강 평면도 각 48,000바이트(세 버퍼 합계 144,000바이트)를 동적 할당한다. BUSY 시간 초과 30초, POWER OFF 이후 DEEP SLEEP 전 BUSY 재확인 및 1 ms 대기, 부분창 끝 좌표 보정이 반영되어 있다. 보드 패키지 설치 버전은 빌드 스크립트에서 자동 강제하지 않으므로 설치 목록에서 2.0.17인지 확인한다.

### 새 PC 준비 및 확인

```powershell
# 프로젝트의 esp32 폴더에서, Arduino CLI를 사용하는 경우
arduino-cli version
arduino-cli core list
arduino-cli board list
$index = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
arduino-cli core install esp32:esp32@2.0.17 --additional-urls $index
.\build.ps1 -PrepareIde
```

IDE의 Boards Manager에서 같은 2.0.17을 설치해도 된다. 마지막 줄에 출력된 `ControlRoom.ino`를 IDE에서 열고 ESP32 Dev Module / 실제 USB 포트로 컴파일·업로드한다. CLI가 필요하면 `.\build.ps1`(빌드) 또는 `.\build.ps1 -UploadPort COM4`(COM4가 실제 ESP32 포트일 경우에만)를 사용한다. 업로드 출력의 칩명·리비전, 포트와 `Hash of data verified.` 여부를 기록한다. [전체 설치·전송 안내](../esp32/README.md#빌드와-전송).

## 3. 연결 버전(현재 코드 기준)

| 연결 | 핀/전원 |
|---|---|
| ESP32-L USB-C | PC USB 5 V / USB 직렬 115200 baud |
| ESP32-L P6 | SDA/GPIO21(RX) ← STM32 PB10(TX), SCL/GPIO22(TX) → STM32 PB11(RX), GND 공통. 3V3 비움, P9 비움. |
| 보드 간 UART | 3.3 V TTL, 115200 baud, 8N1, Serial2 |
| PC ↔ STM32 키트 UART | 9600 baud, 8N1, 별도 포트 |
| ESP32-L P5 → DESPI-C02 P2 (1~8) | 3.3V, GND, SDI/MOSI GPIO23, SCK GPIO18, CS GPIO27, D/C GPIO14, RES GPIO12, BUSY GPIO13 |
| 전원 점퍼 | 큰 ESP32-L P3는 3.3V 패널 전원 연결용. 작은 DESPI-C02 P3는 RESE 선택 스위치로 서로 다르다. |
| 패널 | DESPI-C02 P1에 FPC 삽입·잠금; 실물 모델 확인 후 연결 |

전원을 끄고 USB를 분리한 상태에서 점퍼·리본을 조정한다. [배선 사진](display-wiring.png) · [제조사 회로도](https://www.good-display.com/companyfile/909.html).

## 4. 펌웨어 버전과 검증 범위

- 이전 실물 실행 기준: [c767c16](https://github.com/tokengeoji/mcu/commit/c767c1658029a7a191a8f4da516b7b28dcddebd0). ESP32 교체 보드에 전송했고, 검정/흰색/지도 BUSY는 10,158/10,159/10,160 ms로 기록됐다. 90.1초 뒤에도 HOLD 완료 횟수는 3이었다. U 모드 부분 갱신 두 번 약 3.2초, H 전체 갱신 10,159 ms. 이후 하단 회색 흐림 및 적색 도형 소실이 관찰되어 화면 품질은 미해결이다.
- 현재 코드 기준: [d363bd6](https://github.com/tokengeoji/mcu/commit/d363bd6bdd9206f47751f5b7eb26596534f97bea). 기본 HOLD에서 일반 3색 전체 갱신 1회 후 절전, H 한 번 재표시, A 180초 간격 자동 재표시, U 위치·상태 갱신, ?는 조회. 11월 25일 CLI/IDE 빌드 기록 288,473바이트 플래시, 정적 RAM 21,720바이트(동적 이미지 버퍼 별도). PC에서 20분·4,000개 프레임 및 늦은 BUSY 절전 타이밍 시험 통과. 당시 USB 보드가 연결되지 않아 이 수정본의 실제 업로드 및 화면 품질은 검증되지 않았다.
- 패널 사양서의 25°C 전체 갱신 약 18초와 과거 실물 BUSY 측정값은 구분해서 기록한다. 빨강/흰 위치점 유지, 부분 갱신·잔상은 실제 지급 패널에서 검증해야 한다. [패널 사양](https://www.good-display.com/product/394.html).
- v6 기하 좌표는 확정되었으나 센서 실측과 STM32/ESP32 지도 보정은 미완료. 본 문서 추가는 펌웨어·바이너리·라이브러리의 변경이나 실물 업로드를 수행한 것이 아니다.

## 5. 현장 기록 TODO

- [ ] ESP32-L 교체 기판의 전후면/모듈 라벨 및 PCB 리비전 사진, 업로드 로그의 칩명·리비전(두 기존 기록의 차이 해소).
- [ ] e-Ink 후면 제품 라벨/리비전과 지급 명세, 실제 패널 모델, DESPI-C02 RESE 설정을 기록. FPC-C001만으로 모델 확정 금지.
- [ ] IDE·보드 패키지·Arduino CLI 실제 버전, 실제 USB 포트, 빌드에 사용한 프로젝트 커밋과 제조사 커밋 기록.
- [ ] 최신 빌드 업로드 뒤 HOLD 직후와 90초 뒤 동일 조명 사진 및 `?` 응답 기록. H는 180초 간격을 지켜 시험. 적색 위험 표시, 하단 흐림, BUSY/절전 로그 확인.
- [ ] U 모드와 UART 양방향, 센서 위치·지도 및 경고/서보 독립 동작은 [TODO.md](../TODO.md) 기준으로 검증한다.
