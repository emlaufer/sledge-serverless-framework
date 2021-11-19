
#include "flush.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>


#define COOL_DEVICE_NAME "cool"
#define COOL_DEVICE_PATH "/dev/" COOL_DEVICE_NAME

#define COOL_IOCTL_MAGIC_NUMBER (long)0xc31

#define COOL_IOCTL_CMD_BTBF _IOR(COOL_IOCTL_MAGIC_NUMBER, 1, size_t)
#define COOL_IOCTL_CMD_CFLSH _IOR(COOL_IOCTL_MAGIC_NUMBER, 2, size_t)

int btbf = -1;

/**
 * Opens the flush ko device. Returns true on success, false on failure
 */
bool flush_init() {
    if(btbf < 0) {
        btbf = open(COOL_DEVICE_PATH, 0);
        if(btbf < 0) {
            return false;
        }
    }

    return true;
}

// TODO: better error handling...
void btb_flush() {
    if(btbf < 0) {
        printf("Ensure flush_init() is called first.\n");
        abort();
    }
    if(ioctl(btbf, COOL_IOCTL_CMD_BTBF, 0) < 0) {
        printf("Failed to execute ibpb.\n");
        abort();
    }
}

void cflush() {
    if(btbf < 0) {
        printf("Ensure flush_init() is called first.\n");
        abort();
    }
    if(ioctl(btbf, COOL_IOCTL_CMD_CFLSH, 0) < 0) {
        printf("Failed to execute cflsh.\n");
        abort();
    }
} 
