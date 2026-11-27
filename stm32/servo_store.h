#ifndef SERVO_STORE_H
#define SERVO_STORE_H
#include "stm32f10x_flash.h"

/* STM32F103RB, last 1 KiB page. Excluded from the application linker region.
   The writer only programs erased halfwords and never erases a page. */
#define SERVO_PAGE_ADDR 0x0801FC00UL
#define SERVO_PAGE_WORDS 512
#define SERVO_RECORD_WORDS 5
#define SERVO_HEADER 0x4B53
#define SERVO_FORMAT 0x4301
#define SERVO_COMMIT 0xA35C
static const volatile u16 *const servo_page = (const volatile u16 *)SERVO_PAGE_ADDR;

static u16 servo_checksum(u16 low, u16 center, u16 high)
{
    u16 words[3], crc = 0xFFFF;
    int i, bit;
    words[0] = low;
    words[1] = center;
    words[2] = high;
    for (i = 0; i < 3; ++i) {
        crc ^= words[i];
        for (bit = 0; bit < 16; ++bit)
            crc = (u16)((crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1);
    }
    return crc;
}

static int servo_store_valid(u16 low, u16 center, u16 high)
{
    return low >= SERVO_MIN_US && low + TRIM_STEP_US <= center &&
           center + TRIM_STEP_US <= high && high <= SERVO_MAX_US;
}

static int servo_store_load(u16 *low, u16 *center, u16 *high)
{
    int i;
    if (servo_page[0] != SERVO_HEADER || servo_page[1] != SERVO_FORMAT) return 0;
    for (i = SERVO_PAGE_WORDS - SERVO_RECORD_WORDS; i >= 2; i -= SERVO_RECORD_WORDS) {
        const volatile u16 *p = servo_page + i;
        u16 saved_low, saved_center, saved_high;
        if (p[4] != SERVO_COMMIT) continue;
        saved_low = p[0];
        saved_center = p[1];
        saved_high = p[2];
        if (servo_store_valid(saved_low, saved_center, saved_high) &&
            p[3] == servo_checksum(saved_low, saved_center, saved_high)) {
            *low = saved_low;
            *center = saved_center;
            *high = saved_high;
            return 1;
        }
    }
    return 0;
}

static int servo_write_word(int index, u16 value)
{
    return servo_page[index] == 0xFFFF &&
           FLASH_ProgramHalfWord(SERVO_PAGE_ADDR + 2UL * index, value) == FLASH_COMPLETE &&
           servo_page[index] == value;
}

static int servo_store_save(u16 low, u16 center, u16 high)
{
    int i, j, ok;
    u16 old_low, old_center, old_high;
    if (!servo_store_valid(low, center, high)) return 0;
    if (servo_store_load(&old_low, &old_center, &old_high) &&
        old_low == low && old_center == center && old_high == high) return 1;
    if (servo_page[0] != SERVO_HEADER || servo_page[1] != SERVO_FORMAT) {
        /* Allow interrupted first-time header creation only on an otherwise blank page. */
        if (servo_page[0] != 0xFFFF && servo_page[0] != SERVO_HEADER) return 0;
        for (i = 1; i < SERVO_PAGE_WORDS; ++i) if (servo_page[i] != 0xFFFF) return 0;
        FLASH_Unlock();
        ok = (servo_page[0] == SERVO_HEADER || servo_write_word(0, SERVO_HEADER)) &&
             servo_write_word(1, SERVO_FORMAT);
        FLASH_Lock();
        if (!ok) return 0;
    }
    for (i = 2; i + SERVO_RECORD_WORDS <= SERVO_PAGE_WORDS; i += SERVO_RECORD_WORDS) {
        for (j = 0; j < SERVO_RECORD_WORDS; ++j) if (servo_page[i+j] != 0xFFFF) break;
        if (j != SERVO_RECORD_WORDS) continue;
        FLASH_Unlock();
        /* A power loss before the final marker leaves the previous record in effect. */
        ok = servo_write_word(i, low) && servo_write_word(i+1, center) &&
             servo_write_word(i+2, high) && servo_write_word(i+3, servo_checksum(low, center, high)) &&
             servo_write_word(i+4, SERVO_COMMIT);
        FLASH_Lock();
        return ok;
    }
    return 0;
}

#endif
