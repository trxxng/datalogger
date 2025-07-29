#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pico/flash.h"
#include "hardware/flash.h"

#define uart0_tx 0
#define uart0_rx 1
#define FLASH_TARGET_OFFSET (256 * 1024)

static uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

static void call_flash_range_program(void *param) {
    static uint32_t current_size = 0;
    uint32_t offset = ((uintptr_t*)param)[0];
    const uint8_t *data = (const uint8_t *)((uintptr_t*)param)[1];
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
    current_size += strlen((const char *)data);
}

void my_gets(char *buffer, int maxlen) {
    int idx = 0;
    while (idx < maxlen - 1) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            sleep_ms(1);
            continue;
        }
        if (c == '\r' || c == '\n') { // Dừng khi gặp CR hoặc LF
            break;
        }
        buffer[idx++] = (char)c;
    }
    buffer[idx] = '\0';
}

char rx_buffer[64];
int rx_idx = 0;
void on_uart_rx() {
    while (uart_is_readable(uart0)) {
        char ch = uart_getc(uart0);
        rx_buffer[rx_idx++] = ch;
        if (ch == '\n' || ch == '\r' || rx_idx >= sizeof(rx_buffer) - 1) {
            rx_buffer[rx_idx] = '\0';  // Kết thúc chuỗi
            printf("Received: %s", rx_buffer);
            rx_idx = 0;  // Reset
            break;
        }
    }
}

int main() {
    int n = 0;
    char buffer[64];
    uart_init(uart0, 115200); // khoi tao uart0 baudrate 115200
    stdio_usb_init(); // Khoi tao stdio

    gpio_set_function(uart0_tx, GPIO_FUNC_UART);
    gpio_set_function(uart0_rx, GPIO_FUNC_UART);

    uart_set_hw_flow(uart0, false, false);    // Tắt flow control
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);  // 8N1
    uart_set_fifo_enabled(uart0, false);      // Tắt FIFO
    // int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);

    while(1) {
        my_gets(buffer, sizeof(buffer)); // doc chuoi tu stdin
        uart_puts(uart0, buffer); // gui chuoi den uart0
        uart_putc(uart0, '\n'); // gui ky tu xuong dong
        // uart_putc(uart0, '\r'); // gui ky tu xuong dong
        printf("Transmited: %s\n", buffer); // in ra chuoi gui
        // sleep_ms(100); // delay 100ms
    }
}