#include "main.h"

static UART_HandleTypeDef huart2_trace;

void Trace_uart_init_c(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);
    huart2_trace.Instance          = USART2;
    huart2_trace.Init.BaudRate     = 115200;
    huart2_trace.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2_trace.Init.StopBits     = UART_STOPBITS_1;
    huart2_trace.Init.Parity       = UART_PARITY_NONE;
    huart2_trace.Init.Mode         = UART_MODE_TX_RX;
    huart2_trace.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2_trace.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2_trace);
    // Força habilitação no Renode (HAL_UART_Init não funciona no simulador)
        volatile uint32_t *usart2_cr1 = (volatile uint32_t *)0x40004400;
        *usart2_cr1 |= (1U << 0) | (1U << 3);
}

void Trace_uart_transmit_c(const uint8_t *buf, uint16_t len) {
    volatile uint32_t *usart2_cr1 = (volatile uint32_t *)0x40004400;
    *usart2_cr1 |= (1U << 0) | (1U << 3);   // garante UE + TE

    volatile uint32_t *usart2_tdr = (volatile uint32_t *)0x40004428;
    for (uint16_t i = 0; i < len; i++) {
        *usart2_tdr = buf[i];
    }
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //No software real talvez tenha de ser usado HAL UART TRANSMIT
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
}

void Trace_uart_test_renode(void) {
    // Habilita USART2 diretamente no registrador CR1 (bit 0 = UE, bit 3 = TE)
    volatile uint32_t *usart2_cr1 = (volatile uint32_t *)0x40004400;
    *usart2_cr1 |= (1U << 0) | (1U << 3);  // UE + TE

    volatile uint32_t *usart2_tdr = (volatile uint32_t *)0x40004428;
    const char *msg = "TRACE OK\r\n";
    for (int i = 0; msg[i]; i++) {
        *usart2_tdr = (uint8_t)msg[i];
    }
}
