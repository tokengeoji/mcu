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

Windows PowerShell에서 이 디렉터리를 연다. Arduino CLI와 Espressif 보드 코어 2.0.17을 사용한다.

```powershell
$index = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
arduino-cli core update-index --additional-urls $index
arduino-cli core install esp32:esp32@2.0.17 --additional-urls $index
.\build.ps1
```

첫 빌드가 제조사 공식 커밋의 소스 파일을 내려받는다. 같은 리비전으로 다시 빌드할 때는 로컬 캐시를 사용한다. 바이너리는 `.build/bin`에 생성된다. 실제 포트를 확인한 후 `.\build.ps1 -UploadPort COM5`처럼 지정하면 빌드 후 전송한다. CLI 경로와 설정 파일은 `-ArduinoCli`, `-ConfigFile`로 지정할 수 있다. [Espressif 보드 코어](https://github.com/espressif/arduino-esp32/releases/tag/2.0.17)

## 화면과 좌표

화면은 첨부 계획서의 네 복도와 빨간 위험 표지를 사용한다. STM32의 `stm32/site_profile.h`를 빌드할 때 함께 가져오므로 모형 실측값 변경 시 두 보드를 모두 다시 빌드한다. S1·S3은 수평, S2·S4는 수직이다. 양의 x는 오른쪽, 양의 y는 아래쪽이다. 각 복도의 양 끝에 실측 범위를 대응시킨 모식도이며 실제 건물 축척을 뜻하지 않는다. 설정된 축·길이와 맞지 않는 좌표에는 `CHECK MAP`과 수신한 mm 좌표를 표시한다. 위치 미확인 또는 UART 단절 시 점을 지우고, 이전에 유효한 위치를 받았으면 마지막 수신 후 경과시간의 하한을 10초 단위로 표시한다.

수신 태스크가 위치와 상태의 최신값을 덮어 써서 화면 갱신 동안 프레임이 쌓이지 않는다. 연속 약 2.5초 동안 해당 종류의 프레임을 받지 못하면 해당 표시를 미확인으로 바꾼다. 기존 그림이 바뀌려면 패널 갱신 시간이 추가로 필요하다. USB에는 오류 원인을 출력하고 STM32에는 `READY` 또는 `ERROR` 줄을 보낸다.

## 제조사 예제 적용 내역

`build.ps1`은 Good Display ESP32epdx의 커밋 `d5ed3b777bedb9b407ccff09dc810a9248eb7926`에서 GDEY075Z08 전용 EPD 구동 코드, SPI와 GUI를 가져온다. 패널 명령과 파형은 해당 예제에 따른다. 지도, 프로토콜, 화면 변경영역 계산은 프로젝트 계획서에 맞춘 통합 코드다. [GDEY075Z08 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/3-Colors%20%28BWR%29/7.5/GDEY075Z08)

- 제조사 예제의 부분 기준 버퍼는 1,000바이트로 선언되어 있으나 그리기 루프는 전체 48,000바이트를 읽고 쓴다. 패널 크기에 맞춰 할당하고, 흑백·빨강 입력 평면도 각 48,000바이트씩 확보한다.
- 예제의 BUSY 무한 대기는 30초 제한과 태스크 양보로 바꿨다. 시간 초과 후에는 다음 시도에서 전체 갱신으로 화면과 기준 버퍼를 다시 맞춘다.
- 부분창의 끝 좌표는 시작값에 크기를 더한 뒤 한 픽셀을 뺀 포함 좌표다. 원본에서 하위 바이트를 보낼 때 한 번 더 빼던 연산을 제거했다.
- 이전 화면과 새 화면을 바이트 단위로 비교해 이동 전 배경을 복원한다. 변경 창이 빨간 화소와 겹치거나 부분 갱신 누계가 다섯 회를 넘을 때 전체 갱신한다. 실제 패널에서 빨간색 유지, 잔상, 갱신 시간을 확인한다.
