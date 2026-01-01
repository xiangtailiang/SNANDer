/*
 * STM32 SPI Host Driver for SNANDer
 * Implements communication with stm32-snander firmware via USB CDC
 * 
 * Copyright (C) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

#include "stm32_spi.h"

/* Protocol constants */
#define CMD_NOP         0x00
#define CMD_CFG_SPI     0x01
#define CMD_CS_CTRL     0x02
#define CMD_SPI_WRITE   0x03
#define CMD_SPI_READ    0x04
#define CMD_RESET       0xFF

#define STATUS_OK       0x00
#define STATUS_ERR      0x01

#define CS_LOW          0x00
#define CS_HIGH         0x01

/* Device path - can be set via command line argument later, currently hardcoded or environment variable */
static char serial_dev[256] = "/dev/ttyACM0";
static int serial_fd = -1;

/* Set serial device path */
void stm32_set_serial_device(const char *dev) {
    if (dev) {
        strncpy(serial_dev, dev, sizeof(serial_dev) - 1);
    }
}

static int serial_open(const char *dev) {
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        printf("Error opening %s: %s\n", dev, strerror(errno));
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    
    // Blocking read, 1 byte minimum
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    
    tcflush(fd, TCIOFLUSH);
    
    return fd;
}

static void serial_drain(void) {
    int flags = fcntl(serial_fd, F_GETFL, 0);
    fcntl(serial_fd, F_SETFL, flags | O_NONBLOCK);
    
    uint8_t buf[64];
    while (read(serial_fd, buf, sizeof(buf)) > 0) {
        /* Draining */
    }
    
    fcntl(serial_fd, F_SETFL, flags);
}

static int serial_write(const uint8_t *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(serial_fd, data + written, len - written);
        if (res < 0) {
            printf("Serial write error: %s\n", strerror(errno));
            return -1;
        }
        written += res;
    }
    return 0;
}

static int serial_read(uint8_t *data, size_t len) {
    size_t total_read = 0;
    
    while (total_read < len) {
        /* Simple blocking read with VMIN=1 */
        ssize_t res = read(serial_fd, data + total_read, len - total_read);
        if (res < 0) {
            printf("Serial read error: %s\n", strerror(errno));
            return -1;
        }
        if (res == 0) {
            /* EOF */
            return -1;
        }
        total_read += res;
    }
    return 0;
}

/* Helper to send command and receive standard header response */
static int send_command_get_response(uint8_t cmd, uint16_t len, const uint8_t *data) {
    int retries = 3;
    while (retries > 0) {
        uint8_t buf_header[3];
        buf_header[0] = cmd;
        buf_header[1] = len & 0xFF;
        buf_header[2] = (len >> 8) & 0xFF;
        
        /* Flush any garbage before sending new command */
        serial_drain();
        
        if (serial_write(buf_header, 3) < 0) {
             retries--; continue; 
        }
        if (len > 0 && data) {
            if (serial_write(data, len) < 0) {
                 retries--; continue; 
            }
        }
        
        /* Small delay to allow firmware to process and MacOS driver to turn around */
        usleep(20000); // 20ms
        
        /* Read response header: CMD, STATUS, LEN_L, LEN_H */
        uint8_t resp[4];
        if (serial_read(resp, 4) < 0) {
            // printf("Timeout waiting for response to cmd %02x\n", cmd);
            retries--; continue; 
        }
        
        if (resp[0] != cmd) {
            // printf("Invalid response command: %02x expected %02x\n", resp[0], cmd);
            retries--; continue; 
        }
        
        if (resp[1] != STATUS_OK) {
            // printf("Command failed with status: %02x\n", resp[1]);
            retries--; continue; 
        }
        
        /* Consume response payload if any */
        uint16_t rlen = resp[2] | (resp[3] << 8);
        if (rlen > 0) {
            uint8_t *tmp = malloc(rlen);
            if (tmp) {
                serial_read(tmp, rlen);
                free(tmp);
            } else {
                uint8_t b;
                for(int i=0; i<rlen; i++) serial_read(&b, 1);
            }
        }
        
        return 0; /* Success */
    }
    return -1;
}

int stm32_enable_pins(bool enable) {
    uint8_t state = enable ? CS_LOW : CS_HIGH;
    return send_command_get_response(CMD_CS_CTRL, 1, &state);
}

int stm32_spi_send_command(unsigned int writecnt, unsigned int readcnt, 
                          const unsigned char *writearr, unsigned char *readarr) {
    /* Handle Write if any */
    if (writecnt > 0) {
        unsigned int written = 0;
        unsigned int chunk_size = 512;
        
        while (written < writecnt) {
            unsigned int current = writecnt - written;
            if (current > chunk_size) current = chunk_size;
            
            if (send_command_get_response(CMD_SPI_WRITE, current, writearr + written) < 0) {
                return -1;
            }
            written += current;
        }
    }
    
    /* Handle Read if any */
    if (readcnt > 0) {
        unsigned int read_done = 0;
        unsigned int chunk_size = 512;
        
        while (read_done < readcnt) {
            unsigned int current = readcnt - read_done;
            if (current > chunk_size) current = chunk_size;
            
            uint8_t req[3];
            req[0] = CMD_SPI_READ;
            req[1] = current & 0xFF;
            req[2] = (current >> 8) & 0xFF;
            
            int retries = 3; 
            int success = 0;
            while(retries > 0) {
                serial_drain();
                if (serial_write(req, 3) < 0) { retries--; continue; }
                usleep(20000); // 20ms delay
                
                uint8_t resp[4];
                if (serial_read(resp, 4) < 0) { retries--; continue; }
                
                if (resp[0] != CMD_SPI_READ || resp[1] != STATUS_OK) { retries--; continue;}
                
                uint16_t resp_len = resp[2] | (resp[3] << 8);
                if (resp_len != current) {
                    // printf("Read length mismatch\n");
                    retries--; continue;
                }
                
                if (serial_read(readarr + read_done, current) < 0) { retries--; continue; }
                success = 1;
                break;
            }
            
            if (!success) return -1;
            read_done += current;
        }
    }
    
    return 0;
}

int stm32_spi_init(void) {
    const char *dev = getenv("SNANDER_PORT");
    if (!dev) dev = serial_dev;
    
    serial_fd = serial_open(dev);
    if (serial_fd < 0) return -1;
    
    /* Handshake / NOP */
    int retries = 5;
    while(retries-- > 0) {
        if (send_command_get_response(CMD_NOP, 0, NULL) == 0) break;
        sleep(1);
    }
    if (retries < 0) {
        printf("Failed to communicate with STM32\n");
        close(serial_fd);
        serial_fd = -1;
        return -1;
    }
    
    /* Config SPI 1.125MHz */
    uint8_t speed_cfg = 5; // SPI_SPEED_1M125
    send_command_get_response(CMD_CFG_SPI, 1, &speed_cfg);
    
    return 0; /* Success */
}

int stm32_spi_shutdown(void) {
    if (serial_fd >= 0) {
        close(serial_fd);
        serial_fd = -1;
    }
    return 0;
}
