# 관제실 전자종이 화면

대상 기판은 사진의 Good Display ESP32-L과 DESPI-C02다. 빌드는 계획서에 명시된 800 × 480 삼색 GDEY075Z08을 대상으로 한다. 패널 후면 모델이 일치하는지 확인하고 DESPI-C02의 RESE를 0.47 Ω에 맞춘다. [기판](https://www.good-display.com/product/338.html) · [패널](https://www.good-display.com/product/394.html)

## 결선

ESP32-L의 부품 면이 보이도록 USB-C 단자를 위로 둔다. **커넥터 P6**는 오른쪽 빨간 버튼 바로 아래의 긴 노란 10구 소켓이다. **커넥터 P9**는 왼쪽의 작은 3구 소켓 아래에 있는 노란 8구 소켓이며, 이번 UART 배선에서는 비워 둔다. [실물 소켓 사진](../docs/esp32-connectors.png)

| 커넥터 | 위에서 센 구멍 | 기판 인쇄 | 회로도 핀 번호 | 연결할 STM32 단자 |
| --- | --- | --- | --- | --- |
| P6 | 첫째 | SCL | 10번 | PB11 / USART3_RX ← ESP32 GPIO22 / UART2 TX |
| P6 | 둘째 | SDA | 9번 | PB10 / USART3_TX → ESP32 GPIO21 / UART2 RX |
| P6 | 셋째 | 3V3 | 8번 | 비움 |
| P6 | 넷째 | GND | 7번 | STM32 공통 GND |

UART는 3.3 V TTL, 115200 bps, 8N1이다. 기판 P5와 DESPI-C02 P2는 핀 1부터 3.3V, GND, SDI(GPIO23), SCK(GPIO18), CS(GPIO27), D/C(GPIO14), RES(GPIO12), BUSY(GPIO13) 순으로 연결한다. P6의 SDA/SCL은 UART 핀으로 설정한다. 부팅 후에는 같은 SPI 버스의 플래시와 폰트 칩 CS(GPIO2·15)를 HIGH로 둔다. [Good Display ESP32-L 회로도](https://www.good-display.com/companyfile/909.html) · [Espressif UART API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)

## 빌드와 전송

ESP32-L 위쪽 USB-C와 PC의 USB 포트를 **데이터 케이블**로 연결하고 보드 스위치를 **ON**에 둔다. 이 케이블이 보드 전원(5 V)과 업로드를 함께 담당한다. STM32와 연결할 전원 기준선은 공통 GND 한 가닥이며 P6의 3V3는 비워 둔다. [Good Display 기판 사양](https://www.good-display.com/product/338.html)

**현재 실습 PC**에서 장치 관리자의 `USB-SERIAL CH340 (COM4)`를 통해 esptool이 `ESP32-D0WD-V3`를 식별했다. COM4를 쓰는 터미널을 닫고, PowerShell을 열어 다음 두 줄을 차례로 실행한다.

```powershell
$tools = Join-Path $env:TEMP 'mcu-arduino-toolchain'
& 'C:\mcu\project\esp32\build.ps1' -ArduinoCli "$tools\arduino-cli.exe" -ConfigFile "$tools\arduino-cli.yaml" -UploadPort COM4
```

빌드가 끝나면 스크립트가 `ControlRoom` 펌웨어를 같은 포트에 전송한다. 쓰기 진행률과 `Hash of data verified.`를 확인한다. 이 PC에서 ControlRoom을 COM4로 전송하고 플래시 기록을 검증했다. [Espressif 업로드 출력 예](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/flashing-firmware.html)

**다른 Windows PC**에서는 Arduino CLI를 설치하고, 프로젝트의 `esp32` 폴더에서 PowerShell을 연다. [Arduino CLI 설치 안내](https://docs.arduino.cc/arduino-cli/installation/)

```powershell
$index = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
arduino-cli core update-index --additional-urls $index
arduino-cli core install esp32:esp32@2.0.17 --additional-urls $index
.\build.ps1
```

첫 빌드가 제조사 공식 커밋의 소스 파일을 내려받는다. 같은 리비전으로 다시 빌드할 때는 로컬 캐시를 사용한다. 바이너리는 `.build/bin`에 생성된다. ESP32-L USB를 뽑은 상태와 연결해 ON에 둔 상태에서 `arduino-cli board list`를 각각 실행한다. 새로 생긴 포트가 예를 들어 `COM7`이면 `.\build.ps1 -UploadPort COM7`을 실행한다. 새 포트가 없으면 데이터 케이블과 장치 관리자에서 CH340 인식 여부를 확인한다. CLI 경로와 설정 파일은 `-ArduinoCli`, `-ConfigFile`로 지정할 수 있다. [Espressif 보드 코어](https://github.com/espressif/arduino-esp32/releases/tag/2.0.17)

## 버스 도착정보가 남거나 지도가 뜨지 않을 때

조립 후 촬영한 패널에는 예전에 표시한 **버스 도착정보**가 남아 있었다. 전자종이는 전원이 끊겨도 마지막 그림을 유지한다. 이 펌웨어는 부팅 후 패널을 초기화해 전체를 흰색으로 한 번 갱신하고, 패널을 절전 상태로 둔다. 3초 후 다시 초기화해 관제 지도를 그린다. [Good Display 패널 특성](https://www.good-display.com/product/394.html)

1. 업로드를 마친 뒤 ESP32-L USB 포트(COM4)를 직렬 터미널에서 **115200 baud, 8N1**로 연다. 시작 로그를 보려면 ESP32-L의 빨간 RESET 버튼을 누른다. 이 장비에서는 `UART: first valid STM32 position frame.`과 `UART: first valid STM32 status frame.`을 모두 수신했다. 이전 시험에서는 초기화 중 BUSY(GPIO13)가 30초간 LOW였다. 새 펌웨어 시험에서는 흰 화면 명령을 전송했지만 갱신을 확인할 BUSY 펄스가 없었다. 흰색 지우기 완료와 지도 전송은 아직 확인되지 않았다. 이번 USB 로그는 `EPD: sending white frame.` 다음에 `EPD: no BUSY pulse on white refresh; check P5 BUSY and panel model.`을 출력했다.
2. **큰 ESP32-L의 P3 전원 점퍼를 먼저 확인한다.** 두 보드 전원을 끄고 USB 케이블도 분리한다. 큰 ESP32-L에서 검은 P5 바로 왼쪽, A4·A5 인쇄 옆의 노란 받침 위 금속 핀 두 개가 P3 전원 점퍼다. 사진에는 두 핀이 노출돼 있다. 실물에도 캡이 없으면 해당 두 핀을 함께 덮는 2핀 쇼트캡을 끼운다. 패널 모델에 맞는 RESE 설정을 확인한 뒤 USB 전원을 다시 연결해 화면을 시험한다. 회로도에서 패널 전원은 3.3 V → P3 → R1(0 Ω) → 3.3V_S2 → P5 1번으로 이어진다. [조립 사진](../docs/처음부터_따라하기.html#p1) · [제조사 회로도](https://www.good-display.com/companyfile/909.html) · [Good Display FAQ 1번](https://www.good-display.com/news/79.html)
3. 화면이 그대로이면 다시 두 보드의 전원을 끄고 USB를 뺀다. ESP32-L 하단의 검은 P5 8구에 DESPI-C02의 P2 8핀을 모두 꽂는다. 한쪽 끝의 3.3V ↔ 3.3V와 반대쪽 끝의 BUSY ↔ BUSY를 함께 맞춘다. DESPI-C02의 흰색 P1에 패널 리본을 반듯하게 끝까지 넣고 잠금쇠를 닫는다. [결선도](../docs/하드웨어_연결.md#esp32-l-p5와-despi-c02-p2)
4. 패널 후면 라벨이나 지급 명세에서 GDEY075Z08을 확인한다. 리본 인쇄 FPC-C001만으로는 패널 모델을 특정할 수 없다. GDEY075Z08이면 작은 DESPI-C02의 P3 은색 RESE 스위치를 0.47 Ω로 맞추고 PC USB를 다시 연결해 스위치를 ON에 둔다. 다른 모델이면 해당 모델용 Good Display 드라이버와 RESE 값을 먼저 확인한다.
5. GDEY075Z08이 정상 응답하면 기존 버스 정보 → 흰 화면 → START / S1과 빨간 HAZARD ZONE이 있는 지도의 순서로 바뀐다. STM32 없이도 이 과정을 시험할 수 있다. STM32가 연결되지 않았을 때 상태란은 STM32 STATUS UNAVAILABLE다. 전원을 다시 켰을 때 USB 로그가 `EPD: panel initialized; sending map.`까지 진행하는지 확인한다. `BUSY LOW for 30 s`가 반복되면 P5·P2 접점, P1 잠금 및 패널 모델을 다시 확인한다.

정상 순서는 아래와 같다. 세 번째 줄은 흰색 갱신 후 패널 절전 단계까지 끝났을 때 출력된다. 네 번째 줄은 3초 뒤 지도용 초기화를 시작할 때 출력된다. 흰색 지우기가 완료되면 다음 전원 켜기나 RESET까지 반복하지 않는다. BUSY 오류가 발생하면 다음 자동 시도는 180초 뒤다. 전원을 끄고 결선을 바로잡은 뒤 다시 켜면 부팅 시점에 다시 시도한다.

```text
EPD: initializing for white clear.
EPD: sending white frame.
EPD: white refresh complete.
EPD: starting full refresh (GDEY075Z08).
EPD: panel initialized; sending map.
```

## 화면과 좌표

화면은 첨부 계획서의 네 복도와 빨간 위험 표지를 사용한다. STM32의 `stm32/site_profile.h`를 빌드할 때 함께 가져오므로 모형 실측값 변경 시 두 보드를 모두 다시 빌드한다. S1·S3은 수평, S2·S4는 수직이다. 양의 x는 오른쪽, 양의 y는 아래쪽이다. 각 복도의 양 끝에 실측 범위를 대응시킨 모식도이며 실제 건물 축척을 뜻하지 않는다. 설정된 축·길이와 맞지 않는 좌표에는 `CHECK MAP`과 수신한 mm 좌표를 표시한다. 위치 미확인 또는 UART 단절 시 점을 지우고, 이전에 유효한 위치를 받았으면 마지막 수신 후 경과시간의 하한을 10초 단위로 표시한다.

수신 태스크가 위치와 상태의 최신값을 덮어 써서 화면 갱신 동안 프레임이 쌓이지 않는다. 연속 약 2.5초 동안 해당 종류의 프레임을 받지 못하면 해당 표시를 미확인으로 바꾼다. 기존 그림이 바뀌려면 패널 갱신 시간이 추가로 필요하다. USB에는 오류 원인을 출력하고 STM32에는 `READY` 또는 `ERROR` 줄을 보낸다.

## 제조사 예제 적용 내역

`build.ps1`은 Good Display ESP32epdx의 커밋 `d5ed3b777bedb9b407ccff09dc810a9248eb7926`에서 GDEY075Z08 전용 EPD 구동 코드, SPI와 GUI를 가져온다. 패널 명령과 파형은 해당 예제에 따른다. 지도, 프로토콜, 화면 변경영역 계산은 프로젝트 계획서에 맞춘 통합 코드다. [GDEY075Z08 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/3-Colors%20%28BWR%29/7.5/GDEY075Z08)

- 제조사 예제의 부분 기준 버퍼는 1,000바이트로 선언되어 있으나 그리기 루프는 전체 48,000바이트를 읽고 쓴다. 패널 크기에 맞춰 할당하고, 흑백·빨강 입력 평면도 각 48,000바이트씩 확보한다.
- 예제의 BUSY 무한 대기는 30초 제한과 태스크 양보로 바꿨다. 시간 초과 후에는 다음 시도에서 전체 갱신으로 화면과 기준 버퍼를 다시 맞춘다.
- 부분창의 끝 좌표는 시작값에 크기를 더한 뒤 한 픽셀을 뺀 포함 좌표다. 원본에서 하위 바이트를 보낼 때 한 번 더 빼던 연산을 제거했다.
- 이전 화면과 새 화면을 바이트 단위로 비교해 이동 전 배경을 복원한다. 변경 창이 빨간 화소와 겹치거나 부분 갱신 누계가 다섯 회를 넘을 때 전체 갱신한다. 실제 패널에서 빨간색 유지, 잔상, 갱신 시간을 확인한다.
