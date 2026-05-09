#include "stm32f4xx.h"
#include <stdint.h>

#define APP_A_BASE_ADDR         0x08008000UL
#define BOOT_FLAG_ADDR          0x08007C00UL
#define BOOT_FLAG_VALUE_UPDATE  0xA5A5A5A5UL
#define SRAM_START              0x20000000UL
#define SRAM_END                0x20020000UL  /* STM32F407: 128KB SRAM */

typedef void (*pFunction)(void);

extern int flash_if_erase_app(uint32_t app_start_addr);
extern int flash_if_write(uint32_t addr, const uint8_t *data, uint32_t len);

static void system_clock_config(void);
static void usart1_init(void);
static void usart1_send_byte(uint8_t data);
static uint8_t usart1_recv_byte(void);
static uint32_t read_boot_flag(void);
static void clear_boot_flag(void);
static int app_is_valid(uint32_t app_addr);
static void jump_to_app(uint32_t app_addr);
static void ota_uart_upgrade(void);

int main(void)
{
    HAL_Init();
    system_clock_config();
    usart1_init();

    uint32_t boot_flag = read_boot_flag();

    if ((boot_flag != BOOT_FLAG_VALUE_UPDATE) && app_is_valid(APP_A_BASE_ADDR)) {
        jump_to_app(APP_A_BASE_ADDR);
    }

    ota_uart_upgrade();

    while (1) {
    }
}

static uint32_t read_boot_flag(void)
{
    return *(volatile uint32_t *)BOOT_FLAG_ADDR;
}

static void clear_boot_flag(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = FLASH_SECTOR_1;
    erase.NbSectors = 1;

    (void)HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();
}

static int app_is_valid(uint32_t app_addr)
{
    uint32_t sp = *(volatile uint32_t *)app_addr;
    uint32_t reset_handler = *(volatile uint32_t *)(app_addr + 4U);

    if ((sp < SRAM_START) || (sp > SRAM_END)) {
        return 0;
    }

    if ((reset_handler < APP_A_BASE_ADDR) || (reset_handler > 0x080FFFFFUL)) {
        return 0;
    }

    return 1;
}

static void jump_to_app(uint32_t app_addr)
{
    uint32_t app_stack = *(volatile uint32_t *)app_addr;
    uint32_t app_reset = *(volatile uint32_t *)(app_addr + 4U);
    pFunction app_entry = (pFunction)app_reset;

    __disable_irq();

    for (uint32_t i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    SCB->VTOR = app_addr;
    __set_MSP(app_stack);

    __enable_irq();
    app_entry();
}

/*
 * 简易串口 OTA 协议骨架：
 * 帧格式：
 * [0]  0x55
 * [1]  CMD
 * [2]  LEN_L
 * [3]  LEN_H
 * [4..N-1] DATA
 * [N]  XOR(从CMD到DATA)
 *
 * CMD:
 * 0x01 START: 擦除 App 分区
 * 0x02 DATA : 数据块写入，DATA[0..3] 为目标地址
 * 0x03 END  : 升级结束，清除标志并尝试跳转
 */
static void ota_uart_upgrade(void)
{
    uint8_t sync, cmd, len_l, len_h, checksum;
    uint16_t len;
    uint8_t payload[256];

    while (1) {
        sync = usart1_recv_byte();
        if (sync != 0x55U) {
            continue;
        }

        cmd = usart1_recv_byte();
        len_l = usart1_recv_byte();
        len_h = usart1_recv_byte();
        len = (uint16_t)(len_l | (len_h << 8));

        if (len > sizeof(payload)) {
            usart1_send_byte(0xEE);
            continue;
        }

        checksum = cmd ^ len_l ^ len_h;
        for (uint16_t i = 0; i < len; i++) {
            payload[i] = usart1_recv_byte();
            checksum ^= payload[i];
        }

        uint8_t rx_checksum = usart1_recv_byte();
        if (rx_checksum != checksum) {
            usart1_send_byte(0xE1);
            continue;
        }

        if (cmd == 0x01U) {
            if (flash_if_erase_app(APP_A_BASE_ADDR) == 0) {
                usart1_send_byte(0x79);
            } else {
                usart1_send_byte(0x1F);
            }
        } else if (cmd == 0x02U) {
            if (len < 4U) {
                usart1_send_byte(0xE2);
                continue;
            }

            uint32_t addr = (uint32_t)payload[0] |
                            ((uint32_t)payload[1] << 8) |
                            ((uint32_t)payload[2] << 16) |
                            ((uint32_t)payload[3] << 24);

            if (flash_if_write(addr, &payload[4], len - 4U) == 0) {
                usart1_send_byte(0x79);
            } else {
                usart1_send_byte(0x1F);
            }
        } else if (cmd == 0x03U) {
            clear_boot_flag();
            usart1_send_byte(0x79);

            if (app_is_valid(APP_A_BASE_ADDR)) {
                jump_to_app(APP_A_BASE_ADDR);
            }
        } else {
            usart1_send_byte(0xE0);
        }
    }
}

/* 以下为硬件初始化占位实现，根据实际工程补全 */
static void system_clock_config(void) {}
static void usart1_init(void) {}
static void usart1_send_byte(uint8_t data) { (void)data; }
static uint8_t usart1_recv_byte(void) { return 0x00U; }
