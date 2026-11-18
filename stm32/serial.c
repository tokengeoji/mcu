/* SPL V1 USART / NVIC. Each UART has one IRQ producer for completed RX
   lines and one foreground producer for TX. Publication follows copying. */
#include "station.h"
#include <string.h>

#define RX_SLOTS 4
#define RX_MASK (RX_SLOTS - 1)
typedef struct {
    USART_TypeDef *device;
    volatile u8 tx_head, tx_tail;
    volatile char tx[256];
    volatile u8 rx_head, rx_tail;
    char lines[RX_SLOTS][LINE_CAPACITY];
    u8 length, discard;
    volatile u8 dropped;
} SerialPort;
static SerialPort ports[2];

static void open_port(u8 index, USART_TypeDef *device, u32 baud, u8 irq)
{
    USART_InitTypeDef format;
    ports[index].device = device;
    USART_StructInit(&format); /* 8 data bits, no parity, 1 stop bit, no flow control */
    format.USART_BaudRate = baud;
    USART_Init(device, &format);
    USART_ITConfig(device, USART_IT_RXNE, ENABLE);
    enable_irq(irq, 2);
    USART_Cmd(device, ENABLE);
}

void serial_init(void)
{
    open_port(PC_PORT, USART1, 9600, USART1_IRQChannel);
    open_port(DISPLAY_PORT, USART3, 115200, USART3_IRQChannel);
}

static void receive_byte(SerialPort *p, u8 ch)
{
    char *line = p->lines[p->rx_head];
    u8 next;
    if (ch == '\r' || ch == '\n') {
        if (p->length || p->discard) {
            next = (p->rx_head + 1) & RX_MASK;
            if (next != p->rx_tail) {
                if (p->discard) line[0] = '!';
                line[p->discard ? 1 : p->length] = 0;
                p->rx_head = next;
            } else {
                ++p->dropped;
            }
        }
        p->length = p->discard = 0;
    } else if (ch < 32 || ch > 126 || p->length >= LINE_CAPACITY - 1) {
        p->discard = 1;
    } else if (!p->discard) {
        line[p->length++] = (char)ch;
    }
}

void serial_interrupt(u8 index)
{
    SerialPort *p = &ports[index];
    USART_TypeDef *device = p->device;
    /* RM0008 requires an SR read followed by a DR read to clear ORE/FE/NE/PE.
       Use V1 SPL flag reads for the status and a single USART_ReceiveData. */
    if (USART_GetFlagStatus(device, USART_FLAG_RXNE) != RESET ||
        USART_GetFlagStatus(device, USART_FLAG_ORE) != RESET) {
        u8 errors = USART_GetFlagStatus(device, USART_FLAG_ORE) != RESET ||
                    USART_GetFlagStatus(device, USART_FLAG_FE) != RESET ||
                    USART_GetFlagStatus(device, USART_FLAG_NE) != RESET ||
                    USART_GetFlagStatus(device, USART_FLAG_PE) != RESET;
        u8 ch = (u8)USART_ReceiveData(device);
        if (errors) p->discard = 1;
        else receive_byte(p, ch);
    }
    if (USART_GetITStatus(device, USART_IT_TXE) != RESET) {
        u8 tail = p->tx_tail;
        if (tail != p->tx_head) {
            USART_SendData(device, (u8)p->tx[tail]);
            ++p->tx_tail;
        } else {
            USART_ITConfig(device, USART_IT_TXE, DISABLE);
        }
    }
}

int serial_line(u8 index, char *out)
{
    static u8 reported_drops[2];
    SerialPort *p = &ports[index];
    u8 tail = p->rx_tail;
    if (reported_drops[index] != p->dropped) {
        reported_drops[index] = p->dropped;
        strcpy(out, "!");
        return 1;
    }
    if (p->rx_head == tail) return 0;
    strcpy(out, p->lines[tail]);
    p->rx_tail = (tail + 1) & RX_MASK;
    return 1;
}

int serial_send(u8 index, const char *text)
{
    SerialPort *p = &ports[index];
    unsigned length = strlen(text);
    u8 head = p->tx_head;
    if (length > (u8)(p->tx_tail - head - 1)) return 0;
    while (*text) p->tx[head++] = *text++;
    p->tx_head = head;
    USART_ITConfig(p->device, USART_IT_TXE, ENABLE);
    return 1;
}
