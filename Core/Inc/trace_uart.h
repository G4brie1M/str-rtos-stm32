#ifndef TRACE_UART_H_
#define TRACE_UART_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void Trace_uart_init_c(void);
void Trace_uart_transmit_c(const uint8_t *buf, uint16_t len);
void Trace_uart_test_renode(void);
#ifdef __cplusplus
}
#endif
#endif
