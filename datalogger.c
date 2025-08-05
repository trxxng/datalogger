/**
 * Nguyễn Văn Trường
 *
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#define uart0_tx 0
#define uart0_rx 1
#define FLASH_TARGET_OFFSET (256 * 1024) //(PICO_FLASH_SIZE_BYTES - 64*FLASH_SECTOR_SIZE)
#define TOTAL_PAGES (FLASH_TARGET_OFFSET / FLASH_PAGE_SIZE) // 1024 pages
char buf[256];  // 256 bytes buffer for strings
int *p, addr;
unsigned int page; // prevent comparison of unsigned and signed int
int first_empty_page =-1;

void scan_flash_pages() {
    // Read the flash using memory-mapped addresses
    // For that we must skip over the XIP_BASE worth of RAM
    for(page = 0; page < TOTAL_PAGES; page++){
        addr = XIP_BASE + FLASH_TARGET_OFFSET + (page * FLASH_PAGE_SIZE);
        p = (int *)addr;
        printf("First four bytes of page %d", page);
        printf(" (at 0x%08x) = ", (unsigned int)p);
        printf("%d\n", *p);
        
        if(*p == -1){
            first_empty_page = page;
            printf("First empty page is #%d\n", first_empty_page);
            break; // Stop at the first empty page
        }
    }
}

void read_flash_strings() {
    // Đọc string từ flash thay vì int
    for(page = 0; page < TOTAL_PAGES; page++){
        addr = XIP_BASE + FLASH_TARGET_OFFSET + (page * FLASH_PAGE_SIZE);
        char *string_addr = (char *)addr;
        
        // Kiểm tra xem có dữ liệu hợp lệ không (không phải toàn 0xFF)
        if(string_addr[0] != (char)0xFF && string_addr[0] != '\0') {
            printf("Page %d: %s\n", page, string_addr);
        } else break; // Dừng khi gặp trang trống hoặc chuỗi rỗng
    }
}

void write_to_flash(char *input_buffer) {
    if(strlen(input_buffer) >= sizeof(buf) - 1) {
        printf("Input too long!\n");
        return;
    }
    // Thay vì gán số, bạn có thể sao chép string vào buf
    strcpy(buf, input_buffer);
    if (first_empty_page < 0){
        printf("Full sector, erasing...\n");
        printf("Please type 'erase' to clear all data, or use 'read' to backup data first.\n");
        return;
    }
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(FLASH_TARGET_OFFSET + (first_empty_page*FLASH_PAGE_SIZE), (uint8_t *)buf, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    first_empty_page++; // Move to next empty page
    if (first_empty_page >= TOTAL_PAGES) {
        first_empty_page = -1; // Mark sector as full
    }
}

void my_page(int *q){
    *q = -1;
    for(page = 0; page < TOTAL_PAGES; page++){
    addr = XIP_BASE + FLASH_TARGET_OFFSET + (page * FLASH_PAGE_SIZE);
    p = (int *)addr;
        if(*p == -1){
            *q = page; // Trả về trang trống
            printf("First empty page is #%d\n", *q);
            break;
        }
    }
    if(*q == -1){
        printf("Sector is full, no empty page found.\n");
    }
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
char rx_buffer[256];
int rx_idx = 0;
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
    char buffer[256];
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
    
    my_page(&first_empty_page); // Cập nhật vị trí trang trống
    while(1) {
        my_gets(buffer, sizeof(buffer)); // doc chuoi tu stdin
        if(strcmp(buffer, "read") == 0) {
            read_flash_strings(); // Đọc string từ flash
            continue; // Bỏ qua phần ghi dữ liệu
        }
        if(strcmp(buffer, "erase") == 0){
            uint32_t interrupts = save_and_disable_interrupts();
            flash_range_erase(FLASH_TARGET_OFFSET, 64 * FLASH_SECTOR_SIZE);
            restore_interrupts(interrupts);
            first_empty_page = 0; // Đưa về trang đầu tiên
            continue; // Bỏ qua phần còn lại
        }
        write_to_flash(buffer); // Ghi chuỗi vào flash

        uart_puts(uart0, buffer); // gui chuoi goc den uart0
        uart_putc(uart0, '\n'); // gui ky tu xuong dong
        
        // Flush UART buffer và delay để đảm bảo dữ liệu được gửi hoàn toàn
        uart_tx_wait_blocking(uart0); // Chờ until TX complete
        sleep_ms(50); // Tăng delay lên 50ms để IC nhận có đủ thời gian
        
        printf("Transmitted: %s\n", buffer); // in ra chuoi goc
        // sleep_ms(100); // delay 100ms
    }
}