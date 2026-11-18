# STM32 관제 펌웨어

한백전자 HBE-MCU-MULTI의 STM32F103RBT6용 C 프로젝트다. IAR에서 `Getting_start.eww`를 열고 `EMB_FLASH` 구성을 빌드한다. `EMB_FLASH/Exe/Getting_start.hex`가 수업의 DFU 변환에 사용할 Intel HEX 파일이다. 벡터 테이블은 `0x08003000`에 있다.

## 첫 연결과 입력

1. 전원을 끈 상태에서 아래 표와 배선계획을 대조한다. 초음파 ECHO 레벨, 공통 GND, 서보용 전원 용량을 실물에서 확인한다.
2. 키트 UART USB 단자를 PC에 연결하고 9600 baud, 8N1, 흐름제어 없음으로 연다. `RAW`와 줄 끝 문자를 보내면 `R,S1_mm,S2_mm,S3_mm,S4_mm,S5_mm` 순서로 마지막 거리 추정값이 돌아온다. `-1`은 아직 측정이 없거나 Echo가 유효하지 않다는 뜻이다. 빈 복도와 표적을 놓은 상태에서 각각 반복 측정한다.
3. 아래 절차에 따라 `site_profile.h`를 채우고 다시 빌드한다. 최초 설정에서 두 LED는 꺼지고, 위치는 미확인, 경보는 `UNKNOWN`, 서보는 `UNSET`이다. 복도 위치와 경보는 서로 분리되어 있다.
4. `SAFE`, `CAUTION`, `DANGER`, `ARRIVED` 중 한 줄을 보내면 `OK`를 반환한다. LED0/1은 각각 켜짐/꺼짐, 점멸/꺼짐, 동시 점멸, 둘 다 켜짐이다. 점멸은 500 ms마다 바뀐다. 알 수 없는 줄은 `ERR COMMAND`로 응답한다. 줄 끝은 CR, LF 또는 CRLF를 쓸 수 있다.
5. 운영자가 `ARRIVED`로 도착을 허가한 후 숫자 0을 누르면 보정된 열림 펄스 폭을 적용한다. 문 상태는 명령한 PWM 목표를 뜻한다. 4행을 1 ms 간격으로 스캔하고 5번의 같은 판정 후 눌림을 한 번 처리한다. 문은 열린 상태를 유지한다. 리셋 시 설정된 닫힘 펄스로 시작하므로 기구와 전원의 상태를 먼저 확인한다.

| 용도 | 기본 핀과 주변장치 |
| --- | --- |
| UART1 관제 단말 | PA9 TX → 키트 TX0, PA10 RX ← 키트 RX0 |
| UART3 화면 | PB10 TX → ESP32 RX, PB11 RX ← ESP32 TX, 115200 baud, 8N1 |
| S1~S4 | TRIG PC8~PC11, ECHO PA0~PA3 / TIM2 CH1~4 |
| S5 | TRIG PC12, ECHO PB6 / TIM4 CH1 |
| LED0/1, 피에조 | PA4/PA5, PA6 / TIM3 CH1 |
| 문 서보 | PA8 / TIM1 CH1, 20 ms 주기, MOE 활성 |
| 4×3 키패드 | 열 PC0~2, 행 PC3~6, 숫자 0은 PC6·PC1 |

## 실측 보정

- **거리 범위**. 다섯 센서 모두에서 반복적으로 측정한 최소·최대 유효 거리를 `SITE_RANGE_MIN_MM`, `SITE_RANGE_MAX_MM`에 넣는다. 거리 환산은 배선계획의 근사식인 Echo 왕복 시간(µs) × 343 / 2000을 반올림한 값이다. 60 ms 슬롯, 30 ms timeout, 약 10 µs TRIG는 계획서의 시험 시작값이다.
- **복도**. `site_corridors[0..3]`에 S1~S4 순서로 센서의 전역 좌표, 감지 방향, 통로 길이, 거리 보정량, 빈 복도의 배경 거리, 배경과 표적을 구별할 최소 차이를 mm 단위로 입력한다. 화면 기준 +x는 오른쪽, +y는 아래쪽이다. 방향은 한 축의 +1 또는 −1이다. 보정한 거리와 빈 복도 대비 여유를 동시에 만족해야 위치 후보가 된다. 모서리에서 후보가 여럿이면 미확인으로 전송한다.
- **이동 범위**. 이동 물체의 최대 속도(mm/s)와 위치 오차(mm)를 `SITE_MAX_SPEED_MM_S`, `SITE_POSITION_ERROR_MM`에 넣는다. 최근 위치가 있으면 같은 복도 또는 인접 복도에서 경과시간 내 가능한 이동량을 검사한다. 최대 속도 0이면 속도 검사는 꺼진다. 두 스캔 주기가 지나면 다시 어느 복도에서든 시작할 수 있다.
- **위험 경보**. `SITE_HAZARD_ON_MM`, `SITE_HAZARD_OFF_MM`를 유효 범위 안에 두고, 해제 거리를 접근 거리보다 크게 잡는다. `SITE_HAZARD_SAMPLES`에 시험으로 정한 연속 샘플 수를 넣는다. S5 측정 오류 시 상태는 `FAULT`, 이미 울리던 중이면 `ACTIVE_FAULT`로 보고하며 소리를 유지한다. 경보음은 제공된 PIEZO 예제의 시험음 1024 Hz에 맞췄다.
- **서보**. 사용한 서보의 제품 자료와 무부하 측정으로 확인한 두 펄스 폭(µs)을 `SITE_SERVO_CLOSED_US`, `SITE_SERVO_OPEN_US`에 넣는다. 둘 다 0보다 크고 20 ms보다 작으며 서로 달라야 출력이 켜진다. 이 소프트웨어 검사는 실물 허용 펄스 범위나 기계적 이동 한계를 판정하지 않는다. 실측까지의 기본값은 0이다.

## 소스 대응표

| 파일 | 근거와 역할 |
| --- | --- |
| `Library/inc`, `Library/src` | 수업 예제에 포함된 STMicroelectronics MCD Application Team의 STM32F10x Firmware Library V1.0. 원본 헤더와 필요한 드라이버 구현을 보존했다. |
| `board`, `config`, `Getting_start.*`, `stm32f10x_vector.c`, `stm32f10x_it.*`, `System_func.*` | `4.INTERRUPT/2.Int_timer`의 IAR 시작 코드·링커·벡터가 기반이다. 사용 ISR, 드라이버 목록과 구성 매크로를 변경했다. PLL 기동 대기에 상한을 두고 실제 APB/HCLK에서 타이머 분주값을 계산한다. |
| `hardware.c` | 키패드 행·열은 `2.GPIO(Input)/version_2/2.Keypad/main.c`, PWM 구성은 `3.Timer_PWM_PIEZO/2.PWM`와 `3.PIEZO`에 근거한다. LED 핀 단위 출력, TIM1 서보, TIM2/4 상승·하강 캡처, 트리거 순서와 타임아웃은 배선계획 05·07·08절 및 해당 ST 드라이버의 API를 조합했다. |
| `serial.c`, `main.c` | USART 설정은 `5.UART/version_2/2.INT_UART`와 ST USART 드라이버에서 가져왔다. 비차단 UART 큐, 위치 선택, LED/경보/문 상태와 프레임 생성은 배선계획의 동작을 연결하는 응용 C 코드다. |
| `site_profile.h` | 실물에서 측정할 값을 모아 둔 헤더. 확인되지 않은 거리, 배치, 도어 각도는 0으로 둔다. |

STM32F103xB의 포트 기본 매핑과 타이머 클록은 ST 데이터시트 Table 5와 clock tree를 따른다. 입력 캡처, CCxOF, PWM 출력 활성화 및 USART의 SR→DR 플래그 해제 순서는 ST 레퍼런스 매뉴얼에서 확인할 수 있다. [ST 데이터시트 DS5319](https://www.st.com/resource/en/datasheet/stm32f103rb.pdf) · [ST 레퍼런스 매뉴얼 RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
