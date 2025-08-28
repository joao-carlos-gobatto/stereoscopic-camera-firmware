#ifndef UART_UTILS_H
#define UART_UTILS_H

#include <esp_log.h>
#include <esp_system.h>
#include <stdio.h>  // For sscanf
#include <string.h>
#include <esp_crc.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"

#include "camera_utils.h"

#define UART_PORT_NUM       UART_NUM_0  // Use UART0 for USB-serial
#define TXD_PIN             1           // GPIO1 (TX for USB-serial)
#define RXD_PIN             3           // GPIO3 (RX for USB-serial)
#define UART_BUFFER_SIZE    (4096)
#define CHUNK_SIZE          (2048)

extern QueueHandle_t uart_queue;
extern uint8_t rx_buffer[128];

typedef enum {
    CMD_TAKE_PICTURE = 0,
    CMD_SET_FRAMESIZE = 1,
    CMD_INVALID = 0xFF
} command_t;

void uart_init(void);
void uart_rx_task(void *pvParameters);

#endif // UART_UTILS_H