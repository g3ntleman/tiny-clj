#ifndef TERMIOS_STUB_H
#define TERMIOS_STUB_H

// ESP32 Stub for termios.h - ESP32 doesn't need terminal functionality
// This prevents compilation errors when termios.h is included

#ifdef ESP32_BUILD
// ESP32 doesn't support termios functionality
// All termios functions are stubbed out

struct termios {
    int c_lflag;
    int c_cc[32];  // Character control array
};

#define TCIFLUSH 0
#define TCSAFLUSH 0
#define ICANON 0
#define ECHO 0
#define VMIN 0
#define VTIME 0

// Stub functions
static inline int tcgetattr(int fd, struct termios *termios) { 
    (void)fd; (void)termios; 
    return -1; 
}

static inline int tcsetattr(int fd, int flags, const struct termios *termios) { 
    (void)fd; (void)flags; (void)termios; 
    return -1; 
}

static inline int tcflush(int fd, int flags) { 
    (void)fd; (void)flags; 
    return -1; 
}

#endif // ESP32_BUILD

#endif // TERMIOS_STUB_H
