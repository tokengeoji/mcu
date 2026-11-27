# ESP32-L 전자종이 표시기

STM32가 보낸 최근 감지 거리와 서보 명령각을 7.5인치 흑백 전자종이에 표시한다. 센서 판독, 감지 임계값, 모터와 부저 제어는 STM32가 담당한다. **ScanDisplay는 이번 주제용 새 스케치**다.

## Windows에서 열기

1. PC에 Arduino IDE 2를 설치한다. 설정의 추가 보드 관리자 URL에 `https://espressif.github.io/arduino-esp32/package_esp32_index.json`을 등록하고, Boards Manager에서 **esp32 by Espressif Systems 3.3.12**를 설치한다.
2. ZIP 전체를 압축 해제하고 **`esp32` 폴더에서** PowerShell을 열어 아래를 실행한다. 처음 준비할 때 인터넷 연결이 필요하다.

   ```powershell
   .\build.ps1 -PrepareIde
   ```

3. **마지막 줄에 출력된 `ScanDisplay.ino`**를 연다. 이 파일이 있는 폴더에는 전자종이 드라이버가 같이 준비돼 있다. IDE에서 **ESP32 Dev Module**을 선택하고, ESP32-L을 USB-C 데이터 케이블로 연결해 스위치를 켠 후 새로 나타난 CH340 포트를 선택한다. 컴파일 및 업로드 후 USB 시리얼 모니터 속도는 **115200**이다. 소스 수정은 원래 `ScanDisplay` 폴더에서 하고, 다시 준비하면 수정본을 포함한 새 IDE 폴더 경로가 출력된다.
4. CLI를 쓰면 Arduino CLI 1.6.0 및 같은 3.3.12 코어를 설치한 뒤, `esp32` 폴더에서 `.\build.ps1`로 빌드한다. 업로드 명령은 예를 들어 `.\build.ps1 -UploadPort COM10`이며, COM10은 해당 PC에서 실제로 잡힌 포트로 바꾼다.

## 화면과 수신

- **WAITING FOR STM32**는 부팅 후 유효한 데이터가 아직 없다는 뜻이다. **RECEIVING STM32**는 UART 프레임을 받고 있다는 뜻이다. 3초 이상 유효한 프레임이 끊기면 다음 화면 갱신에서 **STM32 LINK LOST**가 표시된다.
- **LAST DETECTION**은 가장 최근에 STM32가 감지로 판정한 샘플이다. 서보 명령각(도), 초음파 거리(mm), 수신 당시 ESP32 기동 후 시간(초)을 표시한다. 다음 측정이 미감지이더라도 기록은 유지되며 ESP32 재부팅 시 초기화된다. 반원 선의 방향은 명령각, 거리는 숫자로 읽는다.
- STM32의 USART3 TX(PB10) → ESP32 P6 SDA(GPIO21 / Serial2 RX), 공통 GND, **115200 8N1**. RX 수신 전용 표시 펌웨어다. 반대 방향 GPIO22 → PB11은 예약 핀이다. PC USB 디버그는 별도 `Serial`이다.
- ASCII 한 줄은 `SCAN,순번,감지여부,명령각,거리mm\r\n`이다. 예를 들어 `SCAN,42,1,85,248`은 85°의 명령각에서 248 mm 감지를 뜻한다. `SCAN,43,0,90,-1`은 유효한 거리가 없는 샘플이다. 양수 거리와 `감지여부=1`일 때 최근 감지 기록을 갱신한다. 300~350 mm 등의 히스테리시스 판정도 STM32 결과를 따른다.
- 수신 태스크는 패널의 BUSY 대기 중에도 최신 감지 기록을 보관한다. 첫 감지, 4초 이상 감지가 없다가 들어온 감지, 통신 연결 상태 변경을 우선 표시한다. 같은 물체가 반복 감지되면 최신 값을 모아 최대 36초에 한 번 다시 그린다. 화면 작업은 최소 2초 간격이며 부분 갱신 5회 후 전체 갱신을 한다. 전체 갱신 간 최소 180초이므로 이 한도에 도달하면 대기 후 최신 기록을 표시한다. 실시간 알림은 STM32 부저에서 나온다.

## 패널

실물 표시 기록과 같은 **Seeed T075A04 (SKU 104990861, 800×480 흑백) + DESPI-C02 + Good Display ESP32-L** 조합이다. 작은 어댑터의 RESE는 0.47 Ω. 온도나 배선 상태에 따라 갱신 시간은 달라지며 이번 스케치의 패널 실측값은 없다.

설치 스크립트는 Good Display **ESP32epdx 1.0.9** 커밋 `d5ed3b777bedb9b407ccff09dc810a9248eb7926`에서 GDEY075T7 예제와 드라이버를 받는다. 기존 실물 표시 프로젝트의 SPI 4 MHz, BUSY 타임아웃 30초, 전원 차단 후 100 ms 대기, 절전 복귀 시 이전 평면 복원 및 부분창 끝 좌표 수정을 적용했다. 업데이트 실패 시 USB 모니터에서 상태를 볼 수 있으며 180초 후 재시도한다.

[제조사 드라이버](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926) · [기존 실물 구동 기록](https://github.com/tokengeoji/mcu/blob/30c8e7e7654a11f0a6ca7b38d1ab7d4d0241310f/docs/epaper-monochrome-fix.md)

