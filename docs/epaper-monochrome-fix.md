# ESP32 전자종이 흑백 구동 수정 기록

2026년 11월 25일 · `mcu-epaper-monochrome-fix (2).zip`의 ESP32 펌웨어와 첨부 실물 시험 기록 기준

## 결과

사용자가 첨부본을 실행한 실제 패널에서 고정 지도가 깨끗하고 선명하게 표시된다고 확인해 이 흑백 펌웨어를 확정했다. 첨부된 실물 시험 로그에는 전체 갱신과 절전 후 ESP32를 재부팅할 때 추가 갱신 0회로 같은 지도를 유지한 결과가 있다.

| 항목 | 첨부된 실물 시험 기록 |
| --- | --- |
| 제어 보드 | GooDisplay ESP32-L, Arduino FQBN `esp32:esp32:esp32` |
| 어댑터 | DESPI-C02, 작은 어댑터의 P3 RESE `0.47 Ω` |
| 패널 | Seeed Studio `104990861`, T075A04, UC8179, 800 × 480 흑백 |
| 업로드 | USB-SERIAL CH340 `COM10`에서 검증. 포트 번호는 PC에 따라 달라진다. |

패널은 상자 SKU와 Seeed 제품 페이지로 식별했다. 데이터시트 6쪽에 UC8179, 10쪽 표 7-3에 0.47 Ω 저항이 명시된다. [Seeed 제품 페이지](https://www.seeedstudio.com/7-5-Monochrome-ePaper-Display-with-800x480-Pixels-p-5788.html) · [T075A04 데이터시트](https://files.seeedstudio.com/wiki/Other_Display/750-epaper/7.5_inch_Monochrome_ePaper_Display_Datasheet.pdf)

## 문제와 수정 순서

1. **포트 인식**. 초기에는 Windows에 내장 COM1·COM2만 표시됐다. ESP32-L을 USB 데이터 케이블로 연결하고 전원을 켜자 `USB-SERIAL CH340(COM10)`이 나타났다. Arduino CLI로 컴파일, 업로드, 플래시 검증을 수행했다.
2. **재부팅 때 다시 깜빡임**. 기존 `HOLD`에서 갱신이 1회 끝났는데 이후 시리얼에 `POWERON_RESET`이 나타나고 가동시간이 줄었다. 당시 펌웨어는 재부팅마다 전체 화면을 그렸다. 패널 갱신과 절전 절차를 마치면 ESP32의 비휘발성 저장소에 `HOLD` 완료를 기록하도록 수정했다. 다음 부팅은 패널 구동을 건너뛰고, 명시적으로 `H`, `A`, `U`를 입력하면 기록을 지우고 해당 모드를 실행한다. 자발적 재부팅의 하드웨어 원인은 별도로 특정하지 않았다.
3. **흐린 지도와 색상 불일치**. 초기 빌드는 계획서의 3색 GDEY075Z08 드라이버를 사용했다. 실물 SKU가 흑백임을 확인한 뒤 Good Display의 UC8179 흑백 예제를 적용했다. 흰 배경에 검은 선과 굵은 글자를 사용하고 출발점은 검은 이중 원, 위험구역은 검은 점선으로 표시한다. SPI는 4 MHz로 구동한다. [Good Display 흑백 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/2-Colors%20%28BW%29/7.5/GDEY075T7) · [Seeed UC8179 설정](https://github.com/Seeed-Studio/Seeed_GFX/blob/0b13b21f284c9bce3351b394bbd871b688d6aec7/User_Setups/Setup502_Seeed_XIAO_EPaper_7inch5.h)
4. **전원과 재검증**. 작은 DESPI-C02의 P3가 0.47 Ω로 설정됐고 USB가 연결됐음을 사용자와 확인했다. 흑백 펌웨어를 업로드하고 첫 갱신, 패널 절전, 재부팅 후 갱신 0회를 확인했다.

첨부 원본 ZIP의 SHA-256은 `dccf3ecc9a5bc03b5e181f5551507604cee143769f3eec0fd842178268bee2d8`이다.

## 수정 파일

| 파일 | 수정 내용 |
| --- | --- |
| [`ControlRoom.ino`](../esp32/ControlRoom/ControlRoom.ino) | `HOLD` 완료를 Preferences에 저장한다. 재부팅 때 기존 화면을 사용하고 `?` 응답에 가동시간, 리셋 원인, 유지 기록 여부를 출력한다. |
| [`panel.cpp`](../esp32/ControlRoom/panel.cpp) | 흑백 전체 갱신에 지도 한 프레임을 보낸다. 실시간 부분 갱신 때 패널 RAM에 직전 프레임을 복원한 뒤 변경 영역을 전송한다. |
| [`build.ps1`](../esp32/build.ps1) | 고정된 공식 커밋의 흑백 예제와 SPI·GUI 파일을 준비한다. BUSY 타임아웃, 전원 차단 대기, 부분창 끝 좌표 처리를 적용한다. |

## Windows에서 Arduino CLI로 빌드하고 업로드

첨부본의 검증 환경은 Arduino CLI `1.6.0`, Espressif ESP32 코어 `3.3.12`다. 프로젝트 전체를 푼 뒤 루트에서 아래 명령을 실행한다. ESP32 코어가 이미 준비돼 있으면 설치 명령은 건너뛴다. 첫 빌드는 고정된 제조사 소스를 내려받으므로 인터넷 연결이 필요하다.

```powershell
$esp32Index = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
arduino-cli core update-index --additional-urls $esp32Index
arduino-cli core install esp32:esp32@3.3.12 --additional-urls $esp32Index
arduino-cli board list
.\esp32\build.ps1 -UploadPort COM10
arduino-cli monitor --port COM10 --config baudrate=115200
```

`COM10`은 보드를 켰을 때 새로 나타난 포트로 바꾼다. 업로드 전에 해당 포트를 연 다른 터미널을 닫는다. Arduino CLI 경로와 설정이 따로 있으면 `build.ps1`의 `-ArduinoCli`와 `-ConfigFile`로 지정할 수 있다. [Arduino CLI 설치](https://arduino.github.io/arduino-cli/latest/installation/) · [시리얼 모니터 명령](https://arduino.github.io/arduino-cli/1.6/commands/arduino-cli_monitor/)

| 시리얼 입력 | 동작 |
| --- | --- |
| `?` | 상태 조회. 패널을 갱신하지 않는다. |
| `H` | 고정 지도를 한 번 다시 그리고 절전한다. 패널을 바꿨거나 수동으로 다시 그릴 때 사용한다. |
| `A` | 고정 지도를 180초 간격으로 다시 표시한다. |
| `U` | STM32에서 받은 위치와 상태를 표시한다. |

## 첨부 ZIP에 보관된 실물 검증 로그

```text
Hash of data verified.
EPD: refresh command sent; BUSY wait 4743 ms.
EPD: panel power off; deep sleep command sent.
EPD: mode=HOLD completed refreshes=1 uptime=5708 ms reset=1 retained=1
EPD: retaining previous map without refreshing. USB H=redraw, A=auto, U=live.
EPD: mode=HOLD completed refreshes=0 uptime=5311 ms reset=1 retained=1
```

`completed refreshes`는 현재 부팅에서 실제로 끝난 갱신 수다. 재부팅 후 `completed refreshes=0`과 `retained=1`이 함께 표시되면 기존 지도를 유지하는 경로로 진입한 것이다. 첫 전체 갱신 중에는 패널이 잠깐 깜빡일 수 있다. 고정 지도 시험은 ESP32-L 단독으로 진행했다. STM32F103은 센서와 실시간 위치 연동에 쓰며, 흑백 펌웨어 변경 뒤 실시간 모드는 별도로 실물 재시험하지 않았다.
