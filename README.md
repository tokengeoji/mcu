# SAVE · 위험구역 작업자 추적 관제실 v6

**현재 진행 주제: 기존 SAVE 프로젝트 재개 · 2026-11-27**

STM32를 메인 제어기로, ESP32-L을 e-Ink 표시 전용으로 사용하는 기존 SAVE 프로젝트를 이어서 진행한다. 작업 기준은 루트의 [`stm32/`](stm32/), [`esp32/`](esp32/), [`docs/`](docs/)다.

한 개의 모형 작업자가 네 구간의 복도를 이동한다. STM32가 초음파를 측정하고 현장 장치를 제어한다. ESP32-L은 흰 바탕에 검은 관제 지도를 그린다. 기본 HOLD는 표시를 마치면 패널을 절전시키고 완료 여부를 저장한다. 다음 부팅은 기존 지도를 그대로 유지한다. ESP32 USB에 `U`를 보내면 STM32의 실시간 좌표·상태를 표시한다. `H`는 고정 지도 다시 표시, `A`는 180초 간격 자동 재표시, `?`는 모드·완료 횟수 조회다. PC에서 보낸 관제 명령은 LED를 바꾸고, 위험지점의 다섯 번째 센서는 독립적으로 부저를 제어한다. 키패드 `1`~`5`를 누르고 있으면 선택한 센서의 응답도 소리로 확인할 수 있다.

처음 조립할 때는 프로젝트 압축을 모두 풀고 [처음부터 따라 하기](docs/처음부터_따라하기.html)를 브라우저로 연다. 브레드보드 구멍과 모듈 단자 위치, 빌드와 다운로드 순서, 터미널에서 확인할 응답을 그림과 함께 볼 수 있다.

[센서·보드 핀 연결과 하드웨어 도면](docs/하드웨어_연결.md) · [진행 현황과 실물 시험 TODO](docs/프로젝트_진행현황.md) · [팀 배선계획과 동작 미리보기](docs/2조_위험구역_관제실_흐름_배선계획.html)

**최종 제작도 v6:** [55×40 cm 외벽 밀착·중앙 폐쇄 3D 조립도, 평면도, 재단도](docs/foam-v6.md) · [TODO 및 실물 검증 순서](TODO.md).

**최종 e-Ink 지도:** [v6 800×480 흑백 지도와 좌표](docs/epaper-map-v6.md) (새 지도 실물 재시험 필요).

**전자종이 확정본**은 사용자가 실물에서 깨끗하고 선명한 표시를 확인한 7.5인치 흑백 버전이다. [화면·실물 로그](docs/epaper-monochrome-fix.md) · [부품·라이브러리·빌드 버전](docs/esp32-eink-versions.md)

## 빌드와 초기 설정

다른 Windows PC에서는 프로젝트 전체를 새 폴더에 내려받아 압축을 푼 뒤 `stm32/Getting_start.eww`를 연다. 저장소의 `.gitattributes`가 STM32의 C·헤더·어셈블리·IAR 설정 파일을 Windows 개행(CRLF)으로 지정한다. 이전 소스 ZIP을 풀어 사용하던 경우 새 폴더에서 시작하고, 보정한 `site_profile.h` 값이 있으면 옮긴다. STM32는 수업용 IAR EWARM, ESP32는 Arduino IDE 2와 Espressif의 esp32 보드 패키지 3.3.12을 사용한다. [IAR 편집기 개행 설정](https://docs.iar.com/ewarm/10.1x/en/ide-project-management-and-building/the-development-environment/reference-information-on-the-ide/editor-options.html) · [Git 개행 설정](https://git-scm.com/docs/gitattributes#_eol)

| 대상 | 프로젝트 | 시작 방법 |
| --- | --- | --- |
| 한백전자 HBE-MCU-MULTI · STM32F103RBT6 | [IAR 워크스페이스](stm32/Getting_start.eww) | 실습 환경인 IAR Embedded Workbench for Arm에서 `EMB_FLASH`를 선택하고 **Project → Rebuild All**을 실행한다. |
| Good Display ESP32-L · ESP32-S 모듈 | [전자종이 프로젝트](esp32/) | Arduino IDE에서 준비된 `ControlRoom.ino`를 열고 **ESP32 Dev Module**과 실제 USB 포트를 선택한 뒤 **검증(✓)**과 **업로드(→)**를 차례로 누른다. |

STM32는 실습 예제에 동봉된 **STM32F10x Firmware Library V1.0**을 사용한다. 시작 코드, 클록, 인터럽트 벡터, 링커는 키트의 DFU 부트로더 영역을 보존한다. 새 HEX 파일은 `stm32/EMB_FLASH/Exe/Getting_start.hex`에 생성된다. 수업에서 사용하는 DFU File Manager로 HEX를 DFU로 변환하고, 보드를 PROG 모드에 넣은 뒤 DfuSe Demo에서 애플리케이션을 내려받을 수 있다.

**센서 시험용 STM32 펌웨어**는 이 PC의 `C:\mcu\ControlRoom_STM32_초음파피에조.dfu` 또는 [GitHub 릴리스](https://github.com/tokengeoji/mcu/releases/tag/piezo-check-v1)에서 찾을 수 있다. DfuSe Demo의 **Choose → Upgrade**로 이 DFU를 쓰고, 완료 후 PROG에서 손을 뗀 상태로 RESET을 누른다. ESP32-L은 USB-C 데이터 케이블로 PC에 연결하고 스위치를 ON으로 옮긴다. 흑백본의 실물 시험 기록은 CH340(COM10)이며 현재 PC에서 새로 나타나는 포트를 선택한다. 프로젝트의 `esp32` 폴더에서 `.\build.ps1 -PrepareIde`를 실행하고, 마지막 줄에 표시된 `ControlRoom.ino`를 Arduino IDE에서 연다. [IDE 설정·스케치 준비·업로드](esp32/README.md#빌드와-전송)

**센서 1개를 먼저 시험한다.** 키트의 PA6을 PIEZO 입력에 잇고 S1 정면 20~40 cm에 평평한 표적을 둔다. 키패드 `1`을 2초간 누르면 새 ECHO마다 짧게 울린다. 같은 방법으로 `2`~`5`를 각각 S2~S5에서 시험한다. ESP32를 연결하기 전에도 시험할 수 있다. 소리가 없으면 키트 UART 포트를 9600 baud로 열고 `RAW`와 Enter를 보내 해당 열을 확인한다. 모형 치수와 경보 거리, 서보 끝점은 실측 후 [보정 설정](stm32/site_profile.h)에 넣는다. 초기 설정으로도 명령·LED·원시 거리 전송을 시험할 수 있다. 보정 전 위치는 미확인, 경보는 `UNKNOWN`, 문은 `UNSET`이며 서보 펄스를 출력하지 않는다.

## 배선

보드의 핀 인쇄를 기준으로 연결한다. 두 제어 보드는 GND를 공유한다. STM32·ESP32 사이의 신호는 3.3 V TTL이며 보드별 전원 출력은 따로 둔다.

ESP32 단독으로 고정 지도를 확인할 수 있다. v6 입구 아래 `START`에는 검은 테두리와 검은 중심을 가진 출발점, 위험구역에는 검은 점선이 보인다. ESP32 USB에서 115200 baud로 `?`를 보내면 이 부팅의 완료 횟수와 유지 기록(`retained`)이 나온다. 첫 표시 뒤에는 `1`과 `retained=1`, 저장된 지도를 유지한 재부팅 뒤에는 `0`과 `retained=1`이 정상이다. 패널을 교체했거나 기존 그림을 덮어쓰려면 `H`를 보낸다. [배선 사진과 화면 진단](docs/처음부터_따라하기.html#blank-display) · [실물 검증 로그](docs/epaper-monochrome-fix.md)

ESP32-L은 부품 면의 USB-C 단자를 위로 둔다. STM32 점퍼 3개는 모두 오른쪽 빨간 버튼 아래의 노란 **커넥터 P6**에 꽂는다. 위에서 첫째가 `SCL`, 둘째가 `SDA`, 셋째가 `3V3`(비움), 넷째가 `GND`다. 왼쪽의 노란 8구 **커넥터 P9**는 이번 UART 연결에서 비워 둔다. [실물 사진으로 위치 찾기](docs/esp32-connectors.png)

| 장치 | STM32F103RBT6 | 반대편 |
| --- | --- | --- |
| 하이퍼터미널 | PA9 USART1_TX / PA10 USART1_RX | 실습 키트 UART 모듈 TX0 / RX0. USB 직렬 포트를 PC에 연결 |
| 전자종이로 전송 | PB10 USART3_TX | 커넥터 P6 위에서 둘째 **SDA** (9번 핀, GPIO21 / UART2 RX) |
| 전자종이에서 수신 | PB11 USART3_RX | 커넥터 P6 위에서 첫째 **SCL** (10번 핀, GPIO22 / UART2 TX) |
| 보드 간 기준 전위 | GND | 커넥터 P6 위에서 넷째 **GND** (7번 핀) |
| 복도 센서 S1…S4 | TRIG PC8, PC9, PC10, PC11 순서 / ECHO PA0, PA1, PA2, PA3 순서 | 각 센서의 TRIG / ECHO. ECHO는 TIM2_CH1…4 |
| 위험지점 S5 | TRIG PC12 / ECHO PB6 | S5 TRIG / ECHO. PB6은 TIM4_CH1 |
| LED A / B | PA4 / PA5 | 실습 키트 LED0 / LED1 |
| 위험 경보와 센서 확인음 | PA6 TIM3_CH1 | 키트 PIEZO 입력 |
| 문 | PA8 TIM1_CH1 | SG90 신호선. 서보 전원은 정격에 맞는 전원 레일에서 공급하고 GND 공유 |
| 4×3 키패드 | 열 PC0…PC2 / 행 PC3…PC6 | 숫자 0은 PC6 행과 PC1 열 |

센서 전원과 ECHO의 실제 HIGH 전압은 연결 전에 확인한다. 특히 PA0…PA3은 STM32F103RB 데이터시트에서 5 V 허용 핀으로 지정되지 않는다. 5 V ECHO가 나오는 모듈은 레벨을 변환한 뒤 연결한다. [ST 데이터시트의 핀 정의](https://www.st.com/resource/en/datasheet/stm32f103rb.pdf)

실물 구성은 **GooDisplay ESP32-L · DESPI-C02 · Seeed SKU 104990861**이다. 지급품의 상자 SKU와 제조사 자료로 확인한 패널은 **T075A04**(UC8179, 800×480 흑백)다. 어댑터 P2와 기판 P5는 `3.3V / GND / SDI / SCK / CS / D/C / RES / BUSY` 표기를 맞춰 결합한다. 작은 DESPI-C02의 P3 RESE는 **0.47 Ω**다. [ESP32-L 회로도](https://www.good-display.com/companyfile/909.html) · [Seeed 제품 사양](https://www.seeedstudio.com/7-5-Monochrome-ePaper-Display-with-800x480-Pixels-p-5788.html) · [패널 데이터시트](https://files.seeedstudio.com/wiki/Other_Display/750-epaper/7.5_inch_Monochrome_ePaper_Display_Datasheet.pdf)

## 관제 명령과 데이터

PC는 **9600 baud, 8N1, 흐름 제어 없음**으로 연결한다. 명령은 대문자 ASCII 한 줄이다. CR, LF 또는 CRLF로 끝낸다.

| 입력 | LED A | LED B | 효과 |
| --- | --- | --- | --- |
| `SAFE` | 켜짐 | 꺼짐 | 양호 |
| `CAUTION` | 점멸 | 꺼짐 | 경계 |
| `DANGER` | 점멸 | 점멸 | 위험 |
| `ARRIVED` | 켜짐 | 켜짐 | 관제자가 도착을 확인. 이후 키패드 0을 새로 누르면 보정된 서보로 문을 한 번 연다. |
| `RAW` | 유지 | 유지 | `R,S1,S2,S3,S4,S5` 순서로 측정 거리(mm)를 읽는다. 미측정·시간 초과는 -1이다. |

점멸의 시작 설정은 500 ms 켜짐 / 500 ms 꺼짐이다. S5가 설정된 횟수만큼 접근을 검출하면 경보를 켜고, 해제 거리까지 이탈했을 때 끈다. 경보 중 유효한 ECHO를 잃으면 경보를 유지하고 `ACTIVE_FAULT`를 보낸다. 자동 닫힘 시각은 정하지 않았다. 문 보정 후 다음 시연은 보드 리셋 시의 닫힘 위치에서 시작한다.

보드 간 UART는 **115200 baud, 8N1**이다. STM32가 `P,seq,t_ms,corridor,x_mm,y_mm,valid`와 `S,command,alarm,door`를 CRLF로 끝내 송신한다. 구간은 1…4, 미확인 위치는 `corridor=0, x=0, y=0, valid=0`이다. 원점에서 오른쪽이 +x, 아래가 +y다. STM32는 화면의 갱신 완료를 기다리지 않는다.

## 모형 보정과 실물 확인 순서

1. 수신된 `RAW` 값과 줄자로 잰 거리를 비교한다. 빈 복도의 배경 거리, 물체가 있을 때 판별 가능한 거리 차이, 다섯 센서의 공통 유효 범위를 기록한다.
2. 각 구간의 센서 원점(mm), 방향 단위벡터, 유효 길이, 거리 보정, 배경 거리와 판별 여유를 입력한다. 복도 두 곳에 동시에 후보가 생기거나 위치가 끊길 때 미확인으로 표시되는지 본다.
3. 위험지점에서 접근·해제 거리를 각각 측정하고 해제 거리를 더 크게 설정한다. 감지 연속 횟수를 정한 뒤 접근, 이탈, ECHO 분리를 각각 시험한다.
4. 서보에 문을 연결하기 전에 허용되는 닫힘·열림 펄스 폭을 확인한다. 값을 반영해 다시 빌드한 뒤 `ARRIVED`와 새 키패드 0 입력으로 모형 문이 한 번 열리는지 확인한다.
5. ESP32 USB에서 `U`로 실시간 모드를 선택하고 전자종이 전체 갱신 중 네 관제 명령과 S5 감지를 시험한다. 검은 위치 링의 이전 자리, 위험구역 점선, 통신 단절 시 위치 표시를 실물에서 확인한다.

## 구현 근거

- **실습 환경과 SPL** — `C:\mcu`의 IAR 프로젝트, `System_func.*`, `stm32f10x_vector.c`, `STM32F10x_FLASH.icf`, ST 배포 헤더·소스를 기준으로 삼았다. 라이브러리 호출은 트리에 포함한 버전으로 컴파일한다. 타이머 입력 캡처와 USART 동작은 제조사 레퍼런스에 따른다. [ST RM0008](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)
- **장치별 동작** — 제공된 GPIO 입력·출력, PIEZO, 인터럽트, UART 및 모터 실습과 `2조_위험구역_관제실_흐름_배선계획.html`의 배선·추적·관제 절을 사용했다. 5개 센서의 순차 측정, 모형 좌표 추정, 관제 명령과 화면 처리는 이 과제를 위한 통합 코드다.
- **전자종이** — Good Display의 ESP32-L·DESPI-C02와 GDEY075T7 흑백 예제를 기반으로 Seeed T075A04 패널을 구동한다. [ESP32-L 개발 키트](https://kr.good-display.com/news/193.html) · [Good Display ESP32 흑백 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/2-Colors%20%28BW%29/7.5/GDEY075T7)

## 보관 · 서보 회전식 초음파 탐지기

주제 전환 때 작성한 초음파 스캐너는 보관 자료로 남긴다. [스캐너 코드·설명](ultrasonic-scan/) · [배선도 PDF](ultrasonic-scan/docs/wiring.pdf) · [스캐너 ZIP](ultrasonic-scan.zip). 현재 진행하는 SAVE의 코드·배선·시험 기준은 이 문서 상단과 루트의 `stm32/`, `esp32/`, `docs/`를 따른다.
