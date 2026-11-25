# ESP32·e-Ink 하드웨어 및 소프트웨어 버전 기준

2026-11-25 확정. 사용자가 `mcu-epaper-monochrome-fix (2).zip`으로 구동한 흑백 지도의 선명도를 확인했다. 모형 제작 기준은 바닥 55×40 cm의 [v6](foam-v6.md)이다. [첨부본의 실물 검증 로그](epaper-monochrome-fix.md#첨부-zip에-보관된-실물-검증-로그)

## 1. 하드웨어 식별

| 구분 | 모델·규격 | 확인 상태 |
|---|---|---|
| 표시 제어 기판 | Good Display(GooDisplay) **ESP32-L** | 실물 사진에서 기판 표기 확인 |
| ESP32 모듈 | ESP32-S 계열로 기존 README에 기록 | 정확한 모듈 표기/PCB 리비전은 실물 재대조 필요 |
| ESP32 칩 | 이전 기록의 `ESP32-D0WD rev.1.1` 및 `ESP32-D0WD-V3` | 두 기록을 동일 기판의 식별값으로 단정할 수 없어 현재 기판의 정확한 칩·PCB 리비전은 미확정이다. |
| USB-시리얼 | USB-SERIAL CH340 | 흑백본의 실물 시험 포트는 COM10, 이전 실습의 포트는 COM4였다. 현재 PC에서 열리는 포트를 선택한다. |
| e-Ink 어댑터 | Good Display **DESPI-C02** | 실물 사진에서 확인. P2는 ESP32-L P5, P1은 패널 FPC 연결. |
| 리본 인쇄 | **FPC-C001** | 실물 사진의 인쇄. 패널 모델 번호가 아니다. |
| 실물 패널 | **Seeed SKU 104990861 / T075A04 / UC8179**, 7.5인치, 800×480, 흑백 | 첨부본에 기록된 상자 SKU를 Seeed 제품 및 패널 사양서와 대조했다. |
| 패널 구동 저항 | DESPI-C02 RESE **0.47 Ω** | 첨부본 실물 시험 설정과 T075A04 데이터시트 표 7-3이 일치한다. |

[실물 연결 사진](esp32-connectors.png) · [하드웨어 결선](하드웨어_연결.md) · [ESP32-L](https://www.good-display.com/product/338.html) · [DESPI-C02](https://www.good-display.com/product/516.html) · [Seeed 제품](https://www.seeedstudio.com/7-5-Monochrome-ePaper-Display-with-800x480-Pixels-p-5788.html) · [T075A04 데이터시트](https://files.seeedstudio.com/wiki/Other_Display/750-epaper/7.5_inch_Monochrome_ePaper_Display_Datasheet.pdf)

## 2. 재현 가능한 개발 환경

| 항목 | 사용 기준 |
|---|---|
| Arduino IDE | **2.3.11** (기존 실습 PC 기록; 2.x에서 준비된 스케치 사용) |
| Arduino-ESP32 보드 패키지 | **esp32 by Espressif Systems 3.3.12** |
| IDE 보드 선택 | **ESP32 Dev Module** (classic ESP32) |
| CLI FQBN | `esp32:esp32:esp32` |
| Arduino CLI | 첨부본 실물 시험 환경 **1.6.0** |
| 제조사 라이브러리 | **ESP32epdx 1.0.9** — 고정 커밋의 library.properties에서 확인 |
| 제조사 원본 커밋 | `d5ed3b777bedb9b407ccff09dc810a9248eb7926` |
| 전용 예제 | `examples/2-Colors (BW)/7.5/GDEY075T7/EPD.cpp`, `EPD.h` |
| 프로젝트 소스 | `esp32/ControlRoom`, 준비·빌드 스크립트 `esp32/build.ps1` |
| STM32 참고 | STM32F103RBT6 / STM32F10x Firmware Library V1.0(SPL) / IAR EWARM의 EMB_FLASH. IAR 정확한 설치 버전은 미기록. |

[고정 라이브러리 메타데이터](https://github.com/gooddisplayshare/ESP32epdx/blob/d5ed3b777bedb9b407ccff09dc810a9248eb7926/library.properties) · [고정 패널 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/2-Colors%20%28BW%29/7.5/GDEY075T7) · [Espressif 3.3.12](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12) · [프로젝트 빌드 스크립트](../esp32/build.ps1)

빌드 스크립트가 고정 커밋에서 Good Display의 GDEY075T7 흑백 드라이버와 SPI·GUI·글꼴을 준비한다. SPI는 4 MHz, 고정 화면 버퍼는 흑백 이미지 48,000바이트와 직전 프레임 48,000바이트다. 부분 갱신 시 변경 창에 맞는 임시 버퍼를 추가로 쓴다. 전원 차단 명령 뒤 100 ms를 기다리고 BUSY 해제를 확인한 후 절전한다. BUSY 대기 한도는 30초다. 코어 버전은 빌드 전에 설치 목록에서 확인한다.

### 새 PC 준비 및 확인

```powershell
# 프로젝트의 esp32 폴더에서, Arduino CLI를 사용하는 경우
arduino-cli version
arduino-cli core list
arduino-cli board list
$index = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
arduino-cli core install esp32:esp32@3.3.12 --additional-urls $index
.\build.ps1 -PrepareIde
```

IDE의 Boards Manager에서 같은 3.3.12를 설치해도 된다. 마지막 줄에 출력된 `ControlRoom.ino`를 IDE에서 열고 ESP32 Dev Module / 실제 USB 포트로 컴파일·업로드한다. CLI가 필요하면 `.\build.ps1`(빌드) 또는 `.\build.ps1 -UploadPort COM10`(COM10이 실제 ESP32 포트일 경우)를 사용한다. 업로드 출력의 칩명·리비전, 포트와 `Hash of data verified.` 여부를 기록한다. [전체 설치·전송 안내](../esp32/README.md#빌드와-전송).

## 3. 연결 버전(현재 코드 기준)

| 연결 | 핀/전원 |
|---|---|
| ESP32-L USB-C | PC USB 5 V / USB 직렬 115200 baud |
| ESP32-L P6 | SDA/GPIO21(RX) ← STM32 PB10(TX), SCL/GPIO22(TX) → STM32 PB11(RX), GND 공통. 3V3 비움, P9 비움. |
| 보드 간 UART | 3.3 V TTL, 115200 baud, 8N1, Serial2 |
| PC ↔ STM32 키트 UART | 9600 baud, 8N1, 별도 포트 |
| ESP32-L P5 → DESPI-C02 P2 (1~8) | 3.3V, GND, SDI/MOSI GPIO23, SCK GPIO18, CS GPIO27, D/C GPIO14, RES GPIO12, BUSY GPIO13 |
| 전원 점퍼 | 큰 ESP32-L P3는 3.3V 패널 전원 연결용. 작은 DESPI-C02 P3는 RESE 선택 스위치로 서로 다르다. |
| 패널 | DESPI-C02 P1에 FPC 삽입·잠금; 상자 SKU와 P3 RESE 설정 대조 |

전원을 끄고 USB를 분리한 상태에서 점퍼·리본을 조정한다. [배선 사진](display-wiring.png) · [제조사 회로도](https://www.good-display.com/companyfile/909.html).

## 4. 펌웨어와 시험 이력

| 버전 | 검증한 범위 |
|---|---|
| **확정 흑백본** | 사용자가 실물 화면이 깨끗하고 선명하다고 확인했다. 첨부 로그에는 CH340(COM10) 업로드 검증, 첫 전체 갱신의 BUSY 4,743 ms, 절전 후 재부팅 시 추가 갱신 0회가 기록돼 있다. [펌웨어 및 실물 시험](epaper-monochrome-fix.md) |
| 이전 3색본 `d363bd6` | 당시 계획서의 GDEY075Z08 대상으로 CLI·IDE용 스케치를 각각 빌드했다. 당시 플래시 288,473바이트, 정적 RAM 21,720바이트였으며 PC에서 20분 동안 4,000개 통신 프레임과 늦은 BUSY를 시험했다. [당시 소스](https://github.com/tokengeoji/mcu/commit/d363bd6bdd9206f47751f5b7eb26596534f97bea) |
| 이전 3색본 `c767c16` | 교체 보드 실물에서 검정·흰색·지도 BUSY가 10,158 / 10,159 / 10,160 ms였고 90.1초 뒤 HOLD 완료 횟수가 3이었다. U에서 두 부분 갱신, H에서 전체 갱신을 확인한 뒤 화면 하단이 흐려지는 현상이 관찰됐다. [당시 소스](https://github.com/tokengeoji/mcu/commit/c767c1658029a7a191a8f4da516b7b28dcddebd0) |

## 5. 다음 실물 시험

흑백본은 고정 지도 표시를 확정했다. STM32에서 유효한 위치를 연속으로 보낸 뒤 U 모드에서 검은 위치 링 이동, 이전 자리 복원, 통신 단절·복구를 확인한다. 센서와 서보의 실측, v6 모형 좌표의 적용은 현장 시험에 따른다. [시험 순서](../TODO.md)
