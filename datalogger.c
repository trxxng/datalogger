#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#define uart0_tx 0
#define uart0_rx 1
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - 2*FLASH_SECTOR_SIZE)

char buf[64];  // 64 bytes buffer for strings
int *p, addr;
unsigned int page; // prevent comparison of unsigned and signed int
int first_empty_page;

void scan_flash_pages() {
    // Read the flash using memory-mapped addresses
    // For that we must skip over the XIP_BASE worth of RAM
    for(page = 0; page < FLASH_SECTOR_SIZE/FLASH_PAGE_SIZE; page++){
        addr = XIP_BASE + FLASH_TARGET_OFFSET + (page * FLASH_PAGE_SIZE);
        p = (int *)addr;
        printf("First four bytes of page %d", page);
        printf(" (at 0x%08x) = ", (unsigned int)p);
        printf("%d\n", *p);
        
        if(*p == -1 && first_empty_page < 0){
            first_empty_page = page;
            printf("First empty page is #%d\n", first_empty_page);
        }
    }
}

void read_flash_strings() {
    // Đọc string từ flash thay vì int
    for(page = 0; page < FLASH_SECTOR_SIZE/FLASH_PAGE_SIZE; page++){
        addr = XIP_BASE + FLASH_TARGET_OFFSET + (page * FLASH_PAGE_SIZE);
        char *string_addr = (char *)addr;
        
        // Kiểm tra xem có dữ liệu hợp lệ không (không phải toàn 0xFF)
        if(string_addr[0] != (char)0xFF && string_addr[0] != '\0') {
            printf("Page %d: %s\n", page, string_addr);
        } else if(first_empty_page < 0) {
            first_empty_page = page;
            printf("First empty page is #%d\n", first_empty_page);
        }
    }
}

void write_to_flash(char *input_buffer) {
    // Thay vì gán số, bạn có thể sao chép string vào buf
    strcpy(buf, input_buffer);
    if (first_empty_page < 0){
        printf("Full sector, erasing...\n");
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
        first_empty_page = 0;
        restore_interrupts(ints);
    }
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(FLASH_TARGET_OFFSET + (first_empty_page*FLASH_PAGE_SIZE), (uint8_t *)buf, FLASH_PAGE_SIZE);
    first_empty_page++;
    flash_range_erase(PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    flash_range_program(PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE, (uint8_t *)&first_empty_page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    if (first_empty_page >= FLASH_SECTOR_SIZE/FLASH_PAGE_SIZE) {
        first_empty_page = -1; // Mark sector as full
    }
}
int test;
void what_empty_page(int *q){
    addr = (XIP_BASE + PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
    p = (int *)addr;
    *q = *p; // Trả về địa chỉ của trang trống
    printf(" (at1 0x%08x) = ", (unsigned int)p);
    printf("empty page1= %d\n", *p);
}
// String input function
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
// Interrupt receiver
char rx_buffer[64];
int rx_idx = 0;
bool data_ready = false;
void on_uart_rx() {
    while (uart_is_readable(uart0)) {
        char ch = uart_getc(uart0);
        rx_buffer[rx_idx++] = ch;
        if (ch == '\n' || ch == '\r' || rx_idx >= sizeof(rx_buffer) - 1) {
            rx_buffer[rx_idx] = '\0';  // Kết thúc chuỗi
            printf("Received: %s", rx_buffer);
            rx_idx = 0;  // Reset
            write_to_flash(rx_buffer);  // Đánh dấu dữ liệu đã sẵn sàng
            break;
        }
    }
}

int main() {
    char buffer[64];
    uart_init(uart0, 115200); // Uart0 initialization with baudrate 115200
    stdio_usb_init(); // Uart0 initialization with USB (stdio_init_all: Function printf print on terminal will send string to UART)

    // Delay để đảm bảo kết nối USB/UART ổn định sau khi reconnect
    sleep_ms(1000);
    printf("System ready!\n");

    gpio_set_function(uart0_tx, GPIO_FUNC_UART); // Set GPIO function for UART TX
    gpio_set_function(uart0_rx, GPIO_FUNC_UART); // Set GPIO function for UART RX

    uart_set_hw_flow(uart0, false, false);    // Tắt flow control
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);  // 8N1
    uart_set_fifo_enabled(uart0, false);      // Tắt FIFO
    // int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);
    
    what_empty_page(&first_empty_page); // Lấy trang trống đầu tiên
    while(1) {
        /* if(data_ready) {
            // Xử lý dữ liệu đã nhận từ UART
            data_ready = false; // Reset cờ
            write_to_flash(rx_buffer); // Ghi chuỗi vào flash
            continue; // Bỏ qua phần nhập từ stdin
        } */
        my_gets(buffer, sizeof(buffer)); // doc chuoi tu stdin
        // Copy buffer into myData.some_string
        if(strcmp(buffer, "read") == 0) {
            read_flash_strings(); // Đọc string từ flash
            continue; // Bỏ qua phần ghi dữ liệu
        }
        if(strcmp(buffer, "erase") == 0){
            uint32_t interrupts = save_and_disable_interrupts();
            flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
            //flash_range_program(FLASH_TARGET_OFFSET + (page * FLASH_PAGE_SIZE), myDataAsBytes, FLASH_PAGE_SIZE * writeSize);
            restore_interrupts(interrupts);
            first_empty_page = 0; // Reset first empty page
            continue; // Skip further processing    
        }
        write_to_flash(buffer); // Ghi chuỗi vào flash
        // Chuyển đổi sang binary và ghi vào flash
        /* params[0] = FLASH_TARGET_OFFSET; // set offset
        params[1] = (uintptr_t)binary_buffer; // set dia chi binary buffer (FIXED: was buffer, should be binary_buffer)
        rc = flash_safe_execute(call_flash_range_program, params, UINT32_MAX); // goi ham flash
        hard_assert(rc == 0); // Kiem tra ket qua ghi flash */

        uart_puts(uart0, buffer); // gui chuoi goc den uart0
        uart_putc(uart0, '\n'); // gui ky tu xuong dong
        
        // Flush UART buffer và delay để đảm bảo dữ liệu được gửi hoàn toàn
        uart_tx_wait_blocking(uart0); // Chờ until TX complete
        sleep_ms(50); // Tăng delay lên 50ms để IC nhận có đủ thời gian
        
        printf("Transmitted: %s\n", buffer); // in ra chuoi goc
        // sleep_ms(100); // delay 100ms
    }
}
