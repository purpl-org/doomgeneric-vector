//
// Created by ekeleze on 1/13/26.
//

#include "doomgeneric.h"
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>

#include "doomkeys.h"
#include "doomgeneric.h"

#define SERVER_PORT 666

#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200

#define GPIO_LCD_WRX 110
#define GPIO_LCD_RESET_MIDAS 96
#define XSHIFT 0x0
#define YSHIFT 0x18

static int udp_sock = -1;

static int lcd_fd = -1;
static int MAX_TRANSFER = 0x1000;

static void gpio_export(int pin) {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", pin);
    write(fd, buf, strlen(buf));
    close(fd);
}

static void gpio_set_direction(int pin, const char* dir) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return;
    write(fd, dir, strlen(dir));
    close(fd);
}

static void gpio_write(int pin, int value) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return;
    write(fd, value ? "1" : "0", 1);
    close(fd);
}

static void lcd_spi_transfer(int is_cmd, int bytes, const void* data) {
    const uint8_t* tx_buf = data;

    // DC pin: LOW=command, HIGH=data
    gpio_write(GPIO_LCD_WRX, is_cmd ? 0 : 1);

    while (bytes > 0) {
        size_t count = bytes > MAX_TRANSFER ? MAX_TRANSFER : bytes;
        write(lcd_fd, tx_buf, count);
        bytes -= count;
        tx_buf += count;
    }
}

void DG_Init() {
    printf("Initializing MIDAS LCD (160x80)...\n");

    gpio_export(GPIO_LCD_WRX);
    gpio_export(GPIO_LCD_RESET_MIDAS);
    gpio_set_direction(GPIO_LCD_WRX, "out");
    gpio_set_direction(GPIO_LCD_RESET_MIDAS, "out");

    gpio_write(GPIO_LCD_RESET_MIDAS, 0);
    usleep(50000);
    gpio_write(GPIO_LCD_RESET_MIDAS, 1);
    usleep(120000);

    lcd_fd = open("/dev/spidev1.0", O_RDWR);
    if (lcd_fd < 0) {
        fprintf(stderr, "FUCK: Can't open SPI: %d\n", errno);
        exit(1);
    }

    uint8_t mode = 0;
    ioctl(lcd_fd, SPI_IOC_RD_MODE, &mode);

    int bufsiz_fd = open("/sys/module/spidev/parameters/bufsiz", O_RDONLY);
    if (bufsiz_fd >= 0) {
        char buf[32] = {0};
        read(bufsiz_fd, buf, sizeof(buf));
        MAX_TRANSFER = atoi(buf);
        close(bufsiz_fd);
        printf("SPI max transfer: %d bytes\n", MAX_TRANSFER);
    }

    struct {
        uint8_t cmd;
        uint8_t len;
        uint8_t data[16];
        uint32_t delay;
    } init[] = {
        {0x01, 0, {0}, 150},        // Software reset
        {0x11, 0, {0}, 500},        // Sleep out
        {0x20, 0, {0}, 0},          // Display inversion off
        {0x36, 1, {0xA8}, 0},       // Memory access control
        {0x3A, 1, {0x05}, 0},       // 16-bit RGB565

        {0xE0, 16, {0x07, 0x0e, 0x08, 0x07, 0x10, 0x07, 0x02, 0x07,
                    0x09, 0x0f, 0x25, 0x36, 0x00, 0x08, 0x04, 0x10}, 0},
        {0xE1, 16, {0x0a, 0x0d, 0x08, 0x07, 0x0f, 0x07, 0x02, 0x07,
                    0x09, 0x0f, 0x25, 0x35, 0x00, 0x09, 0x04, 0x10}, 0},

        {0xFC, 1, {128+64}, 0},
        {0x13, 0, {0}, 100},        // Normal display mode
        {0x26, 1, {0x02}, 10},      // Gamma set
        {0x29, 0, {0}, 10},         // Display on

        {0x2A, 4, {(XSHIFT >> 8) & 0xFF, XSHIFT & 0xFF,
                   ((160 + XSHIFT - 1) >> 8) & 0xFF, (160 + XSHIFT - 1) & 0xFF}, 0},
        {0x2B, 4, {(YSHIFT >> 8) & 0xFF, YSHIFT & 0xFF,
                   ((80 + YSHIFT - 1) >> 8) & 0xFF, (80 + YSHIFT - 1) & 0xFF}, 0},

        {0, 0, {0}, 0}
    };

    for (int i = 0; init[i].cmd; i++) {
        lcd_spi_transfer(1, 1, &init[i].cmd);
        if (init[i].len) {
            lcd_spi_transfer(0, init[i].len, init[i].data);
        }
        if (init[i].delay) {
            usleep(init[i].delay * 1000);
        }
    }

    printf("MIDAS LCD initialized!\n");

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    fcntl(udp_sock, F_SETFL, O_NONBLOCK);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

	if (bind(udp_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    	perror("bind");
    	close(udp_sock);
    	return -1;
	}

	printf("Input server initialized!\n");
}

void DG_DrawFrame() {
    static uint16_t rgb565_frame[160 * 80];

    for (int y = 0; y < 80; y++) {
        for (int x = 0; x < 160; x++) {
            int src_x = x * 2;
            int src_y = (y * 5) / 2;
            int src_idx = src_y * 320 + src_x;

            uint32_t pixel = DG_ScreenBuffer[src_idx];

            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;

            uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

            rgb565_frame[y * 160 + x] = (c >> 8) | (c << 8);
        }
    }

    static const uint8_t WRITE_RAM = 0x2C;
    lcd_spi_transfer(1, 1, &WRITE_RAM);
    lcd_spi_transfer(0, sizeof(rgb565_frame), rgb565_frame);
}

void DG_SleepMs(uint32_t ms) {
    usleep(ms * 1000);
}

uint32_t DG_GetTicksMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static unsigned char map_key(char c) {
    if (c == 27) return KEY_ESCAPE;
    if (c == '\n' || c == '\r') return KEY_ENTER;
    if (c == 127 || c == 8) return KEY_BACKSPACE;
    if (c == '\t') return KEY_TAB;
    if (c == ' ') return KEY_USE;
    if (c == 17) return KEY_FIRE;

    if (c == 'w' || c == 'W') return KEY_UPARROW;
    if (c == 's' || c == 'S') return KEY_DOWNARROW;
    if (c == 'a' || c == 'A') return KEY_LEFTARROW;
    if (c == 'd' || c == 'D') return KEY_RIGHTARROW;

    if (c == 'i') return KEY_UPARROW;
    if (c == 'k') return KEY_DOWNARROW;
    if (c == 'j') return KEY_LEFTARROW;
    if (c == 'l') return KEY_RIGHTARROW;

    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c > 0 && c < 128) return c;

    return 0;

}

int DG_GetKey(int *pressed, unsigned char *key)
{
    unsigned char packet[2];
    ssize_t len = recvfrom(udp_sock, packet, 2, 0, NULL, NULL);

    if (len == 2) {
		//printf("%c\n", packet[1]);

        *pressed = packet[0];
        *key = map_key(packet[1]);

        if (*key != 0) {
            return 1;
        }
    }

    return 0;
}

void DG_SetWindowTitle(const char* title) { }
