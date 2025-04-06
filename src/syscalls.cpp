#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <Arduino.h>

extern "C" {
    // Implementation of _write to redirect output to Serial
    int _write(int file, char *ptr, int len) {
        // Only handle stdout and stderr
        if (file != STDOUT_FILENO && file != STDERR_FILENO) {
            errno = EBADF;
            return -1;
        }
        
        // Send data to Serial
        for (int i = 0; i < len; i++) {
            Serial.write(ptr[i]);
        }
        
        return len;
    }
} 