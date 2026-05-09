#include "stm32f4xx.h"

static void system_clock_config(void);
static void app_task(void);

int main(void)
{
    HAL_Init();
    system_clock_config();

    while (1) {
        app_task();
    }
}

static void system_clock_config(void) {}
static void app_task(void) {}
