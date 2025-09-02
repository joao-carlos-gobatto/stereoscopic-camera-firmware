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

// UART Messaging Protocol Message Types
#define RIGHT_STRING "RIGHT\r\n"
#define LEFT_STRING "LEFT\r\n"
#define IMAGE_START_STRING "IMAGE_START\r\n"
#define READY_STRING "READY\r\n"
#define OK_STRING "OK\r\n"
#define ERROR_STRING "ERROR\r\n"

extern QueueHandle_t uart_queue;
extern uint8_t rx_buffer[128];

typedef enum {
    CMD_TAKE_PICTURE = 0,
    CMD_SET_FRAMESIZE = 1,
    CMD_CAMERA_SIDE = 2,
    CMD_INVALID = 0xFF
} command_t;

typedef enum {
    RIGHT = 0,
    LEFT = 1
} camera_side_t;

esp_err_t uart_init(void);
void uart_rx_task(void *pvParameters);

#endif // UART_UTILS_H