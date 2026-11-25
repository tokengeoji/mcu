# 관제실 전자종이 화면

실물에서 선명하게 나온 관제 지도는 **Seeed SKU 104990861 / T075A04 / UC8179 / 800 × 480 흑백** 기준이다. Good Display ESP32-L과 DESPI-C02를 사용하며 작은 어댑터의 P3 RESE는 **0.47 Ω**에 둔다. [확정본과 실물 로그](../docs/epaper-monochrome-fix.md) · [Seeed 제품 사양](https://www.seeedstudio.com/7-5-Monochrome-ePaper-Display-with-800x480-Pixels-p-5788.html) · [패널 데이터시트](https://files.seeedstudio.com/wiki/Other_Display/750-epaper/7.5_inch_Monochrome_ePaper_Display_Datasheet.pdf)

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

1. 아래 준비 절차에서 `build.ps1 -PrepareIde`를 실행한 뒤, 마지막 줄에 나온 `ControlRoom.ino`를 Arduino IDE의 **File → Open**으로 연다. 이 스케치에 Good Display의 GDEY075T7 흑백 예제 드라이버와 라이브러리가 들어 있다.
2. **Tools → Board → esp32 → ESP32 Dev Module**을 선택한다. **Tools → Port**에서는 ESP32-L을 USB로 연결할 때 새로 나타난 포트를 선택한다. 첨부본 실물 기록의 장치는 **USB-SERIAL CH340 (COM10)**이다. 해당 포트를 쓰는 다른 터미널은 닫는다.
3. 왼쪽 위 **Verify(✓)**를 눌러 컴파일 성공을 확인하고 **Upload(→)**를 누른다. 아래 Output 창에 플래시 검사 결과 `Hash of data verified.`와 재시작 메시지가 나타난다. [Arduino IDE 업로드 안내](https://support.arduino.cc/hc/en-us/articles/4733418441116-Upload-a-sketch-in-Arduino-IDE)
4. **Tools → Serial Monitor**에서 **115200 baud**를 선택하고 `?`를 보낸다. 첫 지도 표시 뒤에는 `completed refreshes=1`, `retained=1`이 보인다. 저장된 지도를 유지한 채 재부팅한 경우 `completed refreshes=0`, `retained=1`이다. STM32의 센서 명령 `RAW`는 키트 UART의 별도 포트에서 9600 baud로 보낸다.

### 다른 Windows PC에서 처음 한 번 준비하기

1. Arduino IDE 2를 설치한다. **File → Preferences → Additional boards manager URLs**에 아래 주소를 추가하고 **OK**를 누른다. [Arduino IDE 다운로드](https://www.arduino.cc/en/software/) · [Espressif 보드 패키지 설치 안내](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

2. IDE 왼쪽의 **Boards Manager**에서 `esp32`를 검색한다. **esp32 by Espressif Systems**의 버전 **3.3.12**를 선택해 **Install**을 누른다.
3. 프로젝트 전체를 압축 해제하고 `esp32` 폴더에서 PowerShell을 연 뒤 아래 명령을 실행한다. 제조사 파일을 내려받아 IDE 스케치를 만드는 단계이며 별도 Arduino CLI 설치는 필요 없다.

   ```powershell
   .\build.ps1 -PrepareIde
   ```

4. 마지막 줄에 나온 `ControlRoom.ino`를 IDE의 **File → Open**으로 연 뒤 위 업로드 순서를 따른다. 소스나 `stm32/site_profile.h`를 수정하고 다시 준비하면 기존 스케치를 보존하고 날짜가 붙은 새 폴더 경로를 출력한다.

### PowerShell에서 빌드·전송할 때

첨부 실물 기록의 Arduino CLI는 **1.6.0**, ESP32 코어는 **3.3.12**다. 프로젝트의 `esp32` 폴더에서 명령을 실행한다. 새 PC에서 보드 패키지를 설치하는 예다. [Arduino CLI 설치](https://arduino.github.io/arduino-cli/latest/installation/) · [Espressif 코어 3.3.12](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12)

```powershell
$index = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
arduino-cli core update-index --additional-urls $index
arduino-cli core install esp32:esp32@3.3.12 --additional-urls $index
arduino-cli board list
.\build.ps1
```

빌드 산출물은 `.build/bin`에 생긴다. 업로드하려면 열려 있는 시리얼 모니터를 닫고, 포트 목록에 표시된 ESP32-L이 예를 들어 `COM10`이라면 `.\build.ps1 -UploadPort COM10`을 실행한다. `Hash of data verified.`를 확인한 다음 115200 baud로 모니터를 연다. 별도 Arduino CLI 경로와 설정 파일은 `-ArduinoCli`와 `-ConfigFile`로 지정할 수 있다.

## 버스 도착정보가 남거나 지도가 뜨지 않을 때

1. ESP32 USB에서 `?`를 보낸다. `retained=1`은 이 ESP32가 고정 지도 완료를 저장했다는 뜻이다. 패널을 바꿨거나 화면이 비어 있다면 `H`를 한 번 보내 실제 지도를 전송한다. 같은 부팅에서 직전 전체 갱신 뒤 180초가 지나지 않았으면 대기한다.
2. **큰 ESP32-L의 P3 전원 점퍼를 먼저 확인한다.** 두 보드 전원을 끄고 USB 케이블도 분리한다. 큰 ESP32-L에서 검은 P5 바로 왼쪽, A4·A5 인쇄 옆의 노란 받침 위 금속 핀 두 개가 P3 전원 점퍼다. 사진에는 두 핀이 노출돼 있다. 실물에도 캡이 없으면 해당 두 핀을 함께 덮는 2핀 쇼트캡을 끼운다. 패널 모델에 맞는 RESE 설정을 확인한 뒤 USB 전원을 다시 연결해 화면을 시험한다. 회로도에서 패널 전원은 3.3 V → P3 → R1(0 Ω) → 3.3V_S2 → P5 1번으로 이어진다. [조립 사진](../docs/처음부터_따라하기.html#p1) · [제조사 회로도](https://www.good-display.com/companyfile/909.html) · [Good Display FAQ 1번](https://www.good-display.com/news/79.html)
3. 화면이 그대로이면 다시 두 보드의 전원을 끄고 USB를 뺀다. ESP32-L 하단의 검은 P5 8구에 DESPI-C02의 P2 8핀을 모두 꽂는다. 한쪽 끝의 3.3V ↔ 3.3V와 반대쪽 끝의 BUSY ↔ BUSY를 함께 맞춘다. DESPI-C02의 흰색 P1에 패널 리본을 반듯하게 끝까지 넣고 잠금쇠를 닫는다. [결선도](../docs/하드웨어_연결.md#esp32-l-p5와-despi-c02-p2)
4. 작은 DESPI-C02 P3가 0.47 Ω 위치인지 확인한다. 지급품은 상자 SKU 104990861과 T075A04 데이터시트로 식별했다. 전원을 켠 뒤 ESP32 단독으로도 위쪽 `CONTROL ROOM`, 아래쪽 `MAP LOCKED`, v6 상단 입구 아래 출발점과 오른쪽 위험구역 점선이 보여야 한다. [실물 기록과 데이터시트](../docs/epaper-monochrome-fix.md)

## 화면이 점차 흐려질 때

사용자가 확인한 정상 화면은 [이전 실물 검증판 미리보기](../docs/epaper-startup.png) (새 지도는 [v6 미리보기](../docs/epaper-map-v6.svg))와 비교할 수 있다. 다시 흐려지거나 깜빡일 때는 모드 키를 누르기 전에 `?`를 보내 완료 횟수·가동시간·유지 기록을 남긴다. 같은 조명에서 화면을 비교하면 실제 표시 변화와 ESP32 재부팅 여부를 구분하는 데 도움이 된다. 저장된 지도를 유지한 재부팅은 `EPD: retaining previous map without refreshing.` 메시지와 함께 이번 부팅의 완료 횟수가 0이다. 수동 갱신에서도 BUSY 오류가 반복되면 갱신 중 DESPI-C02 P2의 3.3V–GND 전압 및 위의 결선을 확인한다.

## 화면과 좌표

| ESP32 USB 입력 | 동작 |
| --- | --- |
| `?` | 모드, 이번 부팅에서 완료한 갱신 횟수, 가동시간(ms), 리셋 원인 코드와 고정 지도 완료 기록(`retained`)을 출력한다. |
| `H` | 고정 지도를 한 번 다시 그려 절전하고 완료 기록을 저장한다. 같은 부팅에서 전체 갱신 사이에는 180초를 둔다. |
| `A` | 자동 모드. 시작 지도를 다시 그리고 각 전체 갱신이 끝난 뒤 180초를 기다린다. |
| `U` | 실시간 모드. STM32에서 받은 최신 위치와 상태를 표시한다. |

지도 버전 키는 `hold-foam-v6`으로 변경했다. 새 지도 펌웨어 첫 부팅은 한 번 전체 갱신한다. [v6 변경 기록](../docs/epaper-map-v6.md).

H·A·U는 소문자도 받는다. 기본 모드는 HOLD다. 완료 기록이 있으면 재부팅 때 패널을 구동하지 않고 저장된 화면을 유지한다. H·A·U 중 하나를 입력하면 기존 완료 기록을 먼저 지우고 선택한 모드로 전환한다. `?`는 상태만 조회한다.

고정 출발점에는 흰 띠를 사이에 둔 검은 테두리와 검은 중심이 있으며, 위험구역은 굵은 `HAZARD ZONE` 글자와 검은 점선으로 표시한다. 실시간 모드에서 유효한 위치를 받으면 이동 위치에는 더 큰 검은 빈 링을 그린다. 위치 미확인 또는 UART 단절 시 링을 지운다. 보고 주기 기준 약 2.5초가 지나면 해당 표시를 미확인으로 바꾸며, 이전에 유효한 위치를 받았으면 경과시간을 10초 단위로 표시한다.

화면은 [최종 v6 평면도와 일치하는 지도](../docs/epaper-map-v6.svg)다. S1·S3은 수평, S2·S4는 수직이며 마지막 C4는 위로 향한다. x는 오른쪽, y는 아래쪽으로 증가한다. `stm32/site_profile.h`의 실측 축·길이 및 복도 범위에 맞지 않는 좌표에는 `CHECK MAP`과 수신값을 표시한다. 지도 그림과 픽셀 좌표는 v6으로 변경했으나 실측 보정 및 실시간 보드 간 시험은 남아 있다. 프로파일을 바꾸면 양쪽 보드를 다시 빌드한다. [최종 제작도](../docs/foam-v6.md) · [실측 순서](../TODO.md)

STM32의 위치·상태 UART는 패널 갱신 중에도 별도 태스크가 수신한다. 첫 표시가 정상 완료되거나 유지 기록을 읽은 부팅에서는 STM32로 `READY`를 보낸다. 갱신 오류는 `ERROR`로 보내며, 180초 뒤 재시도해 복구하면 `READY`를 다시 보낸다.

## 제조사 예제 적용 내역

Good Display ESP32epdx 1.0.9의 커밋 `d5ed3b777bedb9b407ccff09dc810a9248eb7926`에서 **GDEY075T7** 흑백 드라이버, SPI·GUI·글꼴을 가져온다. 이 예제 이름과 지급된 패널의 부품명 T075A04는 각각 빌드와 하드웨어 식별에 사용한다. [제조사 흑백 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/2-Colors%20%28BW%29/7.5/GDEY075T7) · [Seeed UC8179 설정](https://github.com/Seeed-Studio/Seeed_GFX/blob/0b13b21f284c9bce3351b394bbd871b688d6aec7/User_Setups/Setup502_Seeed_XIAO_EPaper_7inch5.h)

- 전체 지도는 SPI 4 MHz에서 흑백 이미지 48,000바이트를 사용한다. 마지막으로 성공한 프레임은 별도의 48,000바이트 버퍼에 보관한다.
- 실시간 부분 갱신 전에 직전 프레임을 패널 RAM에 복원한다. 변경 창을 포함해 부분 갱신 누계가 다섯 회를 넘을 때는 전체 갱신으로 전환한다. 부분창 끝 좌표는 시작점과 크기에 맞는 포함 좌표로 보정한다.
- BUSY 대기 한도는 30초다. 패널 전원 차단(0x02) 뒤 100 ms를 기다려 BUSY를 확인한 다음 깊은 절전 명령을 보낸다.
