# 관제실 전자종이 화면

대상 기판은 사진의 Good Display ESP32-L과 DESPI-C02다. 빌드는 계획서에 명시된 800 × 480 삼색 GDEY075Z08을 대상으로 한다. 패널 후면 모델이 일치하는지 확인하고 DESPI-C02의 RESE를 0.47 Ω에 맞춘다. [기판](https://www.good-display.com/product/338.html) · [패널](https://www.good-display.com/product/394.html)

[하드웨어·소프트웨어 버전 기준 및 실제 확인/미확인 항목](../docs/esp32-eink-versions.md) · 제조사 라이브러리 **ESP32epdx 1.0.9**, 고정 커밋은 아래 제조사 예제 적용 내역 참고.

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

### Arduino IDE에서 업로드하기

1. 아래 준비 절차에서 `build.ps1 -PrepareIde`를 실행한 뒤, 마지막 줄에 나온 `ControlRoom.ino`를 Arduino IDE의 **File → Open**으로 연다. 이 스케치에 GDEY075Z08 드라이버와 Good Display 라이브러리가 들어 있다.
2. **Tools → Board → esp32 → ESP32 Dev Module**을 선택한다. **Tools → Port**에서는 ESP32-L을 USB로 연결할 때 새로 나타난 포트를 선택한다. 이 PC의 장치 관리자 표기는 **USB-SERIAL CH340 (COM4)**다. 해당 포트를 쓰는 다른 터미널은 닫는다.
3. 왼쪽 위 **Verify(✓)**를 눌러 컴파일 성공을 확인하고 **Upload(→)**를 누른다. 아래 Output 창에 플래시 검사 결과 `Hash of data verified.`와 재시작 메시지가 나타난다. [Arduino IDE 업로드 안내](https://support.arduino.cc/hc/en-us/articles/4733418441116-Upload-a-sketch-in-Arduino-IDE)
4. **Tools → Serial Monitor**를 열고 속도를 **115200 baud**로 설정한다. ESP32-L의 빨간 RESET을 한 번 눌러 지도 전송 로그를 읽는다. 첫 지도가 완성된 후 `?`를 보내면 `mode=HOLD completed refreshes=1`이 표시된다. STM32의 센서 명령 `RAW`는 키트 UART의 별도 포트에서 9600 baud로 보낸다.

### 다른 Windows PC에서 처음 한 번 준비하기

1. Arduino IDE 2를 설치한다. **File → Preferences → Additional boards manager URLs**에 아래 주소를 추가하고 **OK**를 누른다. [Arduino IDE 다운로드](https://www.arduino.cc/en/software/) · [Espressif 보드 패키지 설치 안내](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

2. IDE 왼쪽의 **Boards Manager**에서 `esp32`를 검색한다. **esp32 by Espressif Systems**의 버전 **2.0.17**을 선택해 **Install**을 누른다.
3. 프로젝트 전체를 압축 해제하고 `esp32` 폴더에서 PowerShell을 연 뒤 아래 명령을 실행한다. 제조사 파일을 내려받아 IDE 스케치를 만드는 단계이며 별도 Arduino CLI 설치는 필요 없다.

   ```powershell
   .\build.ps1 -PrepareIde
   ```

4. 마지막 줄에 나온 `ControlRoom.ino`를 IDE의 **File → Open**으로 연 뒤 위 업로드 순서를 따른다. 소스나 `stm32/site_profile.h`를 수정하고 다시 준비하면 기존 스케치를 보존하고 날짜가 붙은 새 폴더 경로를 출력한다.

### PowerShell에서 빌드·전송할 때 (선택)

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

조립 사진에는 전에 표시한 버스 도착정보가 남아 있다. 정상 동작에서는 부팅 시 흑백과 적색 데이터를 각각 전송하고 일반 3색 전체 갱신을 한 번 실행해 관제 지도를 표시한다. [Good Display 패널 특성](https://www.good-display.com/product/394.html)

1. 업로드 뒤 ESP32-L의 USB 포트를 **115200 baud, 8N1**로 열고 보드의 빨간 RESET을 한 번 누른다. 지도 전체 갱신 완료와 절전 로그가 나오면 `?`로 완료 횟수를 읽는다. 교체 보드는 지난 실습에서 CH340(COM4)으로 인식됐다.
2. **큰 ESP32-L의 P3 전원 점퍼를 먼저 확인한다.** 두 보드 전원을 끄고 USB 케이블도 분리한다. 큰 ESP32-L에서 검은 P5 바로 왼쪽, A4·A5 인쇄 옆의 노란 받침 위 금속 핀 두 개가 P3 전원 점퍼다. 사진에는 두 핀이 노출돼 있다. 실물에도 캡이 없으면 해당 두 핀을 함께 덮는 2핀 쇼트캡을 끼운다. 패널 모델에 맞는 RESE 설정을 확인한 뒤 USB 전원을 다시 연결해 화면을 시험한다. 회로도에서 패널 전원은 3.3 V → P3 → R1(0 Ω) → 3.3V_S2 → P5 1번으로 이어진다. [조립 사진](../docs/처음부터_따라하기.html#p1) · [제조사 회로도](https://www.good-display.com/companyfile/909.html) · [Good Display FAQ 1번](https://www.good-display.com/news/79.html)
3. 화면이 그대로이면 다시 두 보드의 전원을 끄고 USB를 뺀다. ESP32-L 하단의 검은 P5 8구에 DESPI-C02의 P2 8핀을 모두 꽂는다. 한쪽 끝의 3.3V ↔ 3.3V와 반대쪽 끝의 BUSY ↔ BUSY를 함께 맞춘다. DESPI-C02의 흰색 P1에 패널 리본을 반듯하게 끝까지 넣고 잠금쇠를 닫는다. [결선도](../docs/하드웨어_연결.md#esp32-l-p5와-despi-c02-p2)
4. 패널 후면 라벨이나 지급 명세에서 GDEY075Z08을 확인한다. 리본 인쇄 FPC-C001만으로는 패널 모델을 특정할 수 없다. GDEY075Z08이면 작은 DESPI-C02의 P3 은색 RESE 스위치를 0.47 Ω로 맞추고 PC USB를 다시 연결해 스위치를 ON에 둔다. 다른 모델이면 해당 모델용 Good Display 드라이버와 RESE 값을 먼저 확인한다.
5. STM32 연결 전에도 지도의 위쪽에 `CONTROL ROOM`, 아래쪽에 `MAP LOCKED`가 표시된다. 검은 `START / S1` 글자 옆에 검은 테두리와 빨간 내부가 있는 출발점이 보이는지 확인한다. BUSY 시간 초과가 기록되면 P5·P2, P1 리본 잠금, 패널 모델부터 확인한다. 오류 후 다음 시도까지는 180초다.

## 화면이 점차 흐려질 때

1. ESP32 단독으로 부팅하고 시리얼 모니터에서 `?`를 보낸다. `HOLD completed refreshes=1`이 나오면 화면 전체와 아래쪽 위험 표지를 촬영한다. 모드 키를 누르지 않고 90초 뒤 같은 조명과 각도에서 한 번 더 촬영하고 `?` 값을 기록한다. 횟수가 1로 유지되면 그 시간 동안 추가 갱신이 완료되지 않은 것이다.
2. 같은 화면을 다시 그려 비교할 때 `H`를 한 번 보낸다. 전체 갱신 간격 180초가 지나면 한 번 표시한다. 대기 중 반복 명령은 하나로 합친다. `U`는 고정 화면 관찰을 마친 뒤 사용한다.
3. 실내 온도를 기록한다. GDEY075Z08의 25°C 기준 전체 갱신은 약 18초다. 실물 후면 모델명을 확인하고, 갱신 중 작은 DESPI-C02 **P2의 3.3V와 GND 사이**에서 공급 전압을 확인한다. 전원을 분리한 뒤 P1 리본의 수평 삽입·잠금, 큰 보드 P3 전원 점퍼 및 작은 보드 P3의 0.47 Ω 위치를 확인한다. 같은 패널에서 모델 전용 공식 3색 예제도 같은 위치부터 흐려지면 전원·리본·패널 쪽을 점검한다. [GDEY075Z08 사양](https://www.good-display.com/product/394.html) · [제조사 회색·적색 이상 점검](https://www.good-display.com/news/238.html) · [제조사 3색 갱신 간격 FAQ](https://www.good-display.com/news/79.html)

## 화면과 좌표

ESP32 USB 직렬 모니터를 115200 baud로 열고 한 글자를 보낸다. A·H·U는 소문자도 받는다.

| 입력 | 화면 동작 |
| --- | --- |
| `A` | 자동 모드. 제조사 일반 3색 전체 갱신으로 시작 지도를 다시 표시한다. 완료 뒤 180초 간격을 둔다. |
| `H` | 부팅 기본 모드 HOLD. 전체 갱신 간격을 지켜 고정 지도(`MAP LOCKED`)를 한 번 다시 그리고 절전한다. |
| `U` | 최신 STM32 위치와 상태를 표시한다. |
| `?` | 현재 모드와 완료된 패널 갱신 횟수를 출력한다. 조회 자체는 화면을 갱신하지 않는다. |

고정 지도에서 확인할 왼쪽 위 출발 표지는 검은 `START / S1` 글자와 검은 테두리 안 빨간 원이다. 이 표지는 STM32에서 위치를 보내기 전부터 그린다. 실시간 모드에서 STM32가 유효한 위치를 보내면 해당 복도에 검은 테두리의 링을 그린다. 구간 이름과 위험 표지는 24픽셀 글꼴이고, 긴 상태 문장은 20픽셀로 맞춘다. STM32의 `stm32/site_profile.h`를 빌드할 때 함께 가져오므로 모형 실측값 변경 시 두 보드를 모두 다시 빌드한다. S1·S3은 수평, S2·S4는 수직이다. 양의 x는 오른쪽, 양의 y는 아래쪽이다. 각 복도의 양 끝에 실측 범위를 대응시킨 모식도이며 실제 건물 축척을 뜻하지 않는다. 설정된 축·길이와 맞지 않는 좌표에는 `CHECK MAP`과 수신한 mm 좌표를 표시한다. 위치 미확인 또는 UART 단절 시 측정 위치 링을 지우고, 이전에 유효한 위치를 받았으면 마지막 수신 후 경과시간의 하한을 10초 단위로 표시한다.

수신 태스크가 위치와 상태의 최신값을 덮어 써서 화면 갱신 동안 프레임이 쌓이지 않는다. 실시간 모드에서 약 2.5초 동안 해당 종류의 프레임을 받지 못하면 해당 표시를 미확인으로 바꾼다. 기존 그림이 바뀌려면 패널 갱신 시간이 추가로 필요하다. 패널이 첫 지도 갱신에 응답하면 ESP32가 STM32로 `READY`를 한 번 보낸다. 전체 또는 부분 갱신에서 BUSY 응답이 없거나 30초 동안 해제되지 않으면 `ERROR`를 보낸다. STM32는 이를 키트 UART에 `ERR DISPLAY`로 알린다. 180초 후 재시도가 성공하면 ESP32가 다시 `READY`를 보내고 STM32가 상태 한 줄을 즉시 전송한다.

## 제조사 예제 적용 내역

`build.ps1`은 Good Display ESP32epdx의 커밋 `d5ed3b777bedb9b407ccff09dc810a9248eb7926`에서 GDEY075Z08 전용 EPD 구동 코드, SPI와 GUI를 가져온다. 패널 명령과 파형은 해당 예제에 따른다. 지도, 프로토콜, 화면 변경영역 계산은 프로젝트 계획서에 맞춘 통합 코드다. [GDEY075Z08 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/3-Colors%20%28BWR%29/7.5/GDEY075Z08)

- 제조사 예제의 부분 기준 버퍼는 1,000바이트로 선언되어 있으나 그리기 루프는 전체 48,000바이트를 읽고 쓴다. 패널 크기에 맞춰 할당하고, 흑백·빨강 입력 평면도 각 48,000바이트씩 확보한다.
- 예제의 BUSY 무한 대기는 30초 제한과 태스크 양보로 바꿨다. 패널 전원 차단 뒤 BUSY가 늦게 활성화되는 경우를 처리하도록 깊은 절전 명령 직전에 준비 상태를 다시 확인한다. BUSY가 LOW인 동안 명령 전송이 금지되며, 해제 뒤 1 ms를 더 기다린다. [패널 사양서 §5 Note 5-4](https://www.good-display.com/companyfile/664.html) · [UC8179의 POWER OFF·DEEP SLEEP 명령](https://www.good-display.com/companyfile/429.html) 시간 초과 후에는 다음 시도에서 전체 갱신으로 화면과 기준 버퍼를 다시 맞춘다.
- 부분창의 끝 좌표는 시작값에 크기를 더한 뒤 한 픽셀을 뺀 포함 좌표다. 원본에서 하위 바이트를 보낼 때 한 번 더 빼던 연산을 제거했다.
- 이전 화면과 새 화면을 바이트 단위로 비교해 이동 전 배경을 복원한다. 변경 창이 빨간 화소와 겹치거나 부분 갱신 누계가 다섯 회를 넘을 때 전체 갱신한다. 실제 패널에서 빨간색 유지, 잔상, 갱신 시간을 확인한다.
