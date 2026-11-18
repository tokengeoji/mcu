# 2조 위험구역 작업자 추적 관제실

한 개의 모형 작업자가 네 구간의 복도를 이동한다. STM32가 초음파를 측정하고 현장 장치를 제어한다. ESP32-L은 STM32에서 받은 좌표로 전자종이 지도를 갱신한다. PC에서 보낸 관제 명령은 LED를 바꾸고, 위험지점의 다섯 번째 센서는 독립적으로 부저를 제어한다.

[센서·보드 핀 연결과 하드웨어 도면](docs/하드웨어_연결.md) · [진행 현황과 실물 시험 TODO](docs/프로젝트_진행현황.md) · [팀 배선계획과 동작 미리보기](docs/2조_위험구역_관제실_흐름_배선계획.html)

## 빌드와 초기 설정

다른 Windows PC에서는 프로젝트 전체를 새 폴더에 내려받아 압축을 푼 뒤 `stm32/Getting_start.eww`를 연다. 저장소의 `.gitattributes`가 STM32의 C·헤더·어셈블리·IAR 설정 파일을 Windows 개행(CRLF)으로 지정한다. 이전 소스 ZIP을 풀어 사용하던 경우 새 폴더에서 시작하고, 보정한 `site_profile.h` 값이 있으면 옮긴다. STM32는 수업용 IAR EWARM, ESP32는 Arduino CLI와 esp32 코어 2.0.17이 필요하다. [IAR 편집기 개행 설정](https://docs.iar.com/ewarm/10.1x/en/ide-project-management-and-building/the-development-environment/reference-information-on-the-ide/editor-options.html) · [Git 개행 설정](https://git-scm.com/docs/gitattributes#_eol)

| 대상 | 프로젝트 | 시작 방법 |
| --- | --- | --- |
| 한백전자 HBE-MCU-MULTI · STM32F103RBT6 | [IAR 워크스페이스](stm32/Getting_start.eww) | 실습 환경인 IAR Embedded Workbench for Arm에서 `EMB_FLASH`를 선택하고 **Project → Rebuild All**을 실행한다. |
| Good Display ESP32-L · ESP32-S 모듈 | [전자종이 프로젝트](esp32/) | ESP32 전용 프로젝트에서 제조사 드라이버를 준비한 뒤 클래식 ESP32 대상으로 빌드한다. |

STM32는 실습 예제에 동봉된 **STM32F10x Firmware Library V1.0**을 사용한다. 시작 코드, 클록, 인터럽트 벡터, 링커는 키트의 DFU 부트로더 영역을 보존한다. 새 HEX 파일은 `stm32/EMB_FLASH/Exe/Getting_start.hex`에 생성된다. 수업에서 사용하는 DFU File Manager로 HEX를 DFU로 변환하고, 보드를 PROG 모드에 넣은 뒤 DfuSe Demo에서 애플리케이션을 내려받을 수 있다.

**처음에는 PC에서 `RAW`를 입력해 다섯 센서의 측정값부터 확인한다.** 모형 치수와 경보 거리, 서보 끝점은 실측 후 [보정 설정](stm32/site_profile.h)에 넣는다. 초기 설정으로도 명령·LED·원시 거리 전송을 시험할 수 있다. 보정 전 위치는 미확인, 경보는 `UNKNOWN`, 문은 `UNSET`이며 서보 펄스를 출력하지 않는다.

## 배선

보드의 핀 인쇄를 기준으로 연결한다. 두 제어 보드는 GND를 공유한다. STM32·ESP32 사이의 신호는 3.3 V TTL이며 보드별 전원 출력은 따로 둔다.

| 장치 | STM32F103RBT6 | 반대편 |
| --- | --- | --- |
| 하이퍼터미널 | PA9 USART1_TX / PA10 USART1_RX | 실습 키트 UART 모듈 TX0 / RX0. USB 직렬 포트를 PC에 연결 |
| 전자종이로 전송 | PB10 USART3_TX | ESP32-L **P6-9 SDA**, GPIO21을 UART2 RX로 설정 |
| 전자종이에서 수신 | PB11 USART3_RX | ESP32-L **P6-10 SCL**, GPIO22를 UART2 TX로 설정 |
| 보드 간 기준 전위 | GND | ESP32-L **P6-7 GND** |
| 복도 센서 S1…S4 | TRIG PC8, PC9, PC10, PC11 순서 / ECHO PA0, PA1, PA2, PA3 순서 | 각 센서의 TRIG / ECHO. ECHO는 TIM2_CH1…4 |
| 위험지점 S5 | TRIG PC12 / ECHO PB6 | S5 TRIG / ECHO. PB6은 TIM4_CH1 |
| LED A / B | PA4 / PA5 | 실습 키트 LED0 / LED1 |
| 경보음 | PA6 TIM3_CH1 | 키트 PIEZO 입력 |
| 문 | PA8 TIM1_CH1 | SG90 신호선. 서보 전원은 정격에 맞는 전원 레일에서 공급하고 GND 공유 |
| 4×3 키패드 | 열 PC0…PC2 / 행 PC3…PC6 | 숫자 0은 PC6 행과 PC1 열 |

센서 전원과 ECHO의 실제 HIGH 전압은 연결 전에 확인한다. 특히 PA0…PA3은 STM32F103RB 데이터시트에서 5 V 허용 핀으로 지정되지 않는다. 5 V ECHO가 나오는 모듈은 레벨을 변환한 뒤 연결한다. [ST 데이터시트의 핀 정의](https://www.st.com/resource/en/datasheet/stm32f103rb.pdf)

사진에서 기판 **GooDisplay ESP32-L**, 어댑터 **DESPI-C02**, 리본 **FPC-C001**을 판독했다. 어댑터 P2와 기판 P5는 `3.3V / GND / SDI / SCK / CS / D/C / RES / BUSY` 표기를 맞춰 결합한다. 이 프로젝트의 패널 대상은 제공된 계획서의 **GDEY075Z08**(800×480, 흑·백·적)이다. 사진에는 패널 제품 라벨이 보이지 않으므로 최초 구동 전에 후면 라벨이나 지급 명세를 대조한다. 해당 모델의 DESPI-C02 RESE 선택값은 **0.47 Ω**이다. [ESP32-L 회로도](https://www.good-display.com/companyfile/909.html) · [GDEY075Z08 제품 사양](https://www.good-display.com/product/394.html) · [제조사 RESE 선택표 §4.3](https://v4.cecdn.yun300.cn/100001_1909185148/Evaluation%20Kit%20for%20E-Paper%20Display%20DEArduino-L%28C02%29.pdf)

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
5. 전자종이 전체 갱신 중 네 관제 명령과 S5 감지를 시험한다. 흰 점의 이전 자리, 빨간 위험 표지, 통신 단절 시 위치 표시를 실물에서 확인한다.

## 구현 근거

- **실습 환경과 SPL** — `C:\mcu`의 IAR 프로젝트, `System_func.*`, `stm32f10x_vector.c`, `STM32F10x_FLASH.icf`, ST 배포 헤더·소스를 기준으로 삼았다. 라이브러리 호출은 트리에 포함한 버전으로 컴파일한다. 타이머 입력 캡처와 USART 동작은 제조사 레퍼런스에 따른다. [ST RM0008](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)
- **장치별 동작** — 제공된 GPIO 입력·출력, PIEZO, 인터럽트, UART 및 모터 실습과 `2조_위험구역_관제실_흐름_배선계획.html`의 배선·추적·관제 절을 사용했다. 5개 센서의 순차 측정, 모형 좌표 추정, 관제 명령과 화면 처리는 이 과제를 위한 통합 코드다.
- **전자종이** — Good Display가 ESP32-S 모듈과 ESP32-L/DESPI-C02 조합을 명시한다. 패널 명령과 갱신은 해당 패널의 제조사 예제를 기반으로 한다. [ESP32-L 개발 키트](https://kr.good-display.com/news/193.html) · [Good Display ESP32 예제](https://github.com/gooddisplayshare/ESP32epdx/tree/d5ed3b777bedb9b407ccff09dc810a9248eb7926/examples/3-Colors%20(BWR)/7.5/GDEY075Z08)
