#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pico/flash.h"
#include "hardware/flash.h"

#define uart0_tx 0
#define uart0_rx 1
#define FLASH_TARGET_OFFSET (256 * 1024)

static uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

void print_buf(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        printf("%02x", buf[i]);
        if (i % 16 == 15)
            printf("\n");
        else
            printf(" ");
    }
}

static void call_flash_range_erase(void *param) {
    uint32_t offset = (uint32_t)param;
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}

static void call_flash_range_program(void *param) {
    static uint32_t current_size = 0;
    uint32_t offset = ((uintptr_t*)param)[0];
    const uint8_t *data = (const uint8_t *)((uintptr_t*)param)[1];
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
    current_size += strlen((const char *)data);
}

// Hàm chuyển đổi char sang binary
int char_to_binary(const char *input, uint8_t *output, int max_len) {
    int len = strlen(input);
    int output_len = 0;
    
    for (int i = 0; i < len && output_len < max_len - 1; i++) {
        char c = input[i];
        // Chuyển mỗi ký tự thành 8 bit binary
        for (int bit = 7; bit >= 0; bit--) {
            output[output_len++] = ((c >> bit) & 1) + '0'; // Lưu dạng ASCII '0' hoặc '1'
            if (output_len >= max_len - 1) break;
        }
    }
    output[output_len] = '\0'; // Kết thúc chuỗi
    return output_len;
}
// Hàm chuyển đổi binary string về char
int binary_to_char(const char *binary_input, char *output, int max_len) {
    int len = strlen(binary_input);
    int output_len = 0;
    
    // Đảm bảo độ dài binary là bội số của 8
    if (len % 8 != 0) {
        printf("Error: Binary length must be multiple of 8\n");
        return -1;
    }
    
    for (int i = 0; i < len && output_len < max_len - 1; i += 8) {
        char byte_val = 0;
        // Chuyển 8 bit binary thành 1 ký tự
        for (int bit = 0; bit < 8; bit++) {
            if (binary_input[i + bit] == '1') {
                byte_val |= (1 << (7 - bit));
            }
        }
        output[output_len++] = byte_val;
    }
    output[output_len] = '\0';
    return output_len;
}
// Hàm chuyển đổi char sang hex string
int char_to_hex(const char *input, char *output, int max_len) {
    int len = strlen(input);
    int output_len = 0;
    
    for (int i = 0; i < len && output_len < max_len - 3; i++) {
        sprintf(&output[output_len], "%02X", (unsigned char)input[i]);
        output_len += 2;
    }
    output[output_len] = '\0';
    return output_len;
}

// Hàm chuyển đổi hex bytes sang char (readable ASCII only)
void hex_to_char_display(const uint8_t *hex_data, int len) {
    printf("Hex to char: ");
    for (int i = 0; i < len; i++) {
        uint8_t byte = hex_data[i];
        if (byte >= 32 && byte <= 126) { // Printable ASCII range
            printf("%c", (char)byte);
        } else if (byte == 0) {
            printf("\\0"); // Null terminator
        } else {
            printf("\\x%02x", byte); // Non-printable as hex
        }
    }
    printf("\n");
}

// Hàm tạo chuỗi hex từ flash data
void create_hex_string_from_flash(const uint8_t *flash_data, char *hex_string, int data_len, int max_str_len) {
    int str_pos = 0;
    for (int i = 0; i < data_len && str_pos < max_str_len - 3; i++) {
        sprintf(&hex_string[str_pos], "%02x", flash_data[i]);
        str_pos += 2;
    }
    hex_string[str_pos] = '\0';
}

// Hàm chuyển hex string sang char
void hex_string_to_char(const char *hex_string, char *output, int max_len) {
    int hex_len = strlen(hex_string);
    int output_pos = 0;
    
    printf("Converting hex string to char: ");
    for (int i = 0; i < hex_len - 1 && output_pos < max_len - 1; i += 2) {
        char hex_byte[3] = {hex_string[i], hex_string[i+1], '\0'};
        unsigned int byte_val;
        sscanf(hex_byte, "%x", &byte_val);
        
        if (byte_val >= 32 && byte_val <= 126) {
            printf("%c", (char)byte_val);
            output[output_pos++] = (char)byte_val;
        } else if (byte_val == 0) {
            printf("\\0");
        } else {
            printf("\\x%02x", byte_val);
        }
    }
    output[output_pos] = '\0';
    printf("\n");
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
    int rc; // Return code
    char buffer[64];
    char sstr[64];
    int sstr_len = 0;
    uint8_t binary_buffer[512]; // Buffer để chứa dữ liệu binary
    char hex_buffer[128];       // Buffer để chứa dữ liệu hex
    uart_init(uart0, 115200); // Uart0 initialization with baudrate 115200
    stdio_usb_init(); // Uart0 initialization with USB (stdio_init_all: Function printf print on terminal will send string to UART)

    gpio_set_function(uart0_tx, GPIO_FUNC_UART); // Set GPIO function for UART TX
    gpio_set_function(uart0_rx, GPIO_FUNC_UART); // Set GPIO function for UART RX

    uart_set_hw_flow(uart0, false, false);    // Tắt flow control
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);  // 8N1
    uart_set_fifo_enabled(uart0, false);      // Tắt FIFO
    // int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);
    uintptr_t params[2];
    while(1) {
        my_gets(buffer, sizeof(buffer)); // doc chuoi tu stdin
        if(strcmp(buffer, "history") == 0) {
            // Hiển thị lịch sử flash
            printf("Flash history:\n");
            print_buf(flash_target_contents, FLASH_PAGE_SIZE);
            continue;
        }
        if(strcmp(buffer, "erase") == 0) {
            rc = flash_safe_execute(call_flash_range_erase, (void*)FLASH_TARGET_OFFSET, UINT32_MAX);
            hard_assert(rc == 0); // Kiểm tra kết quả xóa flash
            continue;
        }
        if(strcmp(buffer, "read") == 0) {
            printf("Data in flash (string): %s\n", (char*)flash_target_contents);
            printf("Data in flash (hex): ");
            print_buf(flash_target_contents, 32); // Chỉ in 32 byte đầu
            printf("\n");
            
            // Tạo chuỗi hex từ flash data
            char hex_string[128];
            create_hex_string_from_flash(flash_target_contents, hex_string, 32, sizeof(hex_string));
            printf("Hex string: %s\n", hex_string);
            
            // Chuyển hex string sang char
            char converted_chars[64];
            hex_string_to_char(hex_string, converted_chars, sizeof(converted_chars));
            printf("Converted string: %s\n", converted_chars);
            continue;
        }
        
        if(strcmp(buffer, "decode") == 0) {
            printf("=== DECODE BINARY STRING FROM FLASH ===\n");
            // Tìm phần binary string trong flash (bắt đầu từ byte có giá trị 0x30 = '0')
            uint8_t *binary_start = NULL;
            for (int i = 0; i < FLASH_PAGE_SIZE - 8; i++) {
                if (flash_target_contents[i] == 0x30 && flash_target_contents[i+1] == 0x31) {
                    binary_start = &flash_target_contents[i];
                    break;
                }
            }
            
            if (binary_start) {
                printf("Found binary string at offset: %d\n", (int)(binary_start - flash_target_contents));
                printf("Binary string: %s\n", (char*)binary_start);
                
                // Decode binary string về text gốc
                char decoded_text[64];
                int result = binary_to_char((char*)binary_start, decoded_text, sizeof(decoded_text));
                if (result >= 0) {
                    printf("Decoded text: %s\n", decoded_text);
                } else {
                    printf("Failed to decode binary string\n");
                }
            } else {
                printf("No binary string found in flash\n");
            }
            continue;
        }
        
        if(strcmp(buffer, "analyze") == 0) {
            printf("=== FLASH MEMORY ANALYSIS ===\n");
            printf("Full hex dump (first 256 bytes):\n");
            print_buf(flash_target_contents, 256);
            
            printf("\nSearching for readable ASCII text:\n");
            for (int i = 0; i < FLASH_PAGE_SIZE - 1; i++) {
                if (flash_target_contents[i] >= 32 && flash_target_contents[i] <= 126) {
                    // Found printable character, try to find a string
                    int str_len = 0;
                    while (i + str_len < FLASH_PAGE_SIZE && 
                           flash_target_contents[i + str_len] >= 32 && 
                           flash_target_contents[i + str_len] <= 126) {
                        str_len++;
                    }
                    if (str_len >= 2) { // At least 2 characters
                        printf("ASCII at offset %d: ", i);
                        for (int j = 0; j < str_len; j++) {
                            printf("%c", flash_target_contents[i + j]);
                        }
                        printf(" (length: %d)\n", str_len);
                    }
                    i += str_len - 1; // Skip analyzed characters
                }
            }
            
            printf("\nSearching for binary patterns (0x30 0x31 = '01'):\n");
            for (int i = 0; i < FLASH_PAGE_SIZE - 16; i++) {
                if (flash_target_contents[i] == 0x30 && flash_target_contents[i+1] == 0x31) {
                    printf("Binary pattern found at offset %d\n", i);
                    // Print next 32 characters to see the pattern
                    printf("Pattern: ");
                    for (int j = 0; j < 32 && i + j < FLASH_PAGE_SIZE; j++) {
                        printf("%c", flash_target_contents[i + j]);
                    }
                    printf("\n");
                    break; // Just show the first one
                }
            }
            continue;
        }
        // Chuyển đổi sang binary và ghi vào flash
        int binary_len = char_to_binary(buffer, binary_buffer, sizeof(binary_buffer));
        params[0] = FLASH_TARGET_OFFSET; // set offset
        params[1] = (uintptr_t)binary_buffer; // set dia chi binary buffer (FIXED: was buffer, should be binary_buffer)
        rc = flash_safe_execute(call_flash_range_program, params, UINT32_MAX); // goi ham flash
        hard_assert(rc == 0); // Kiem tra ket qua ghi flash
        // Chuyển đổi sang hex để hiển thị
        // char_to_hex(buffer, hex_buffer, sizeof(hex_buffer));
        int sstr_len = binary_to_char((char*)binary_buffer, sstr, sizeof(sstr));
        uart_puts(uart0, buffer); // gui chuoi goc den uart0
        uart_putc(uart0, '\n'); // gui ky tu xuong dong
        /* printf("Original: %s\n", buffer); // in ra chuoi goc
        printf("Binary: %s\n", (char*)binary_buffer); // in ra chuoi binary
        printf("Hex: %s\n", hex_buffer); // in ra chuoi hex */
        printf("Flash write result: %d\n", rc); // in ket qua ghi flash
        printf("Transmitted: %s\n", buffer); // in ra chuoi goc
        printf("Binary: %s\n", (char*)binary_buffer); // in ra chuoi binary
        printf("String: %s\n", sstr); // in ra chuoi hex
        // sleep_ms(100); // delay 100ms
    }
}