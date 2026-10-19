/* Linux terminal endpoints for original XNU console/TTY/VFS code. No Darwin
 * syscall or descriptor handling lives here. Native TLS buffers keep Linux
 * user-copy away from faultable XNU kernel stacks and VM aliases.
 */
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static __thread unsigned char output_byte;
static __thread unsigned char input_byte;
static __thread struct pollfd input_poll;

static _Noreturn void
console_failure(const char *operation)
{
    fprintf(stderr, "XNU_HOST_CONSOLE_ERROR %s errno=%d\n", operation, errno);
    abort();
}

void
serial_putc_options(char c, bool poll)
{
    (void)poll;
    output_byte = (unsigned char)c;
    ssize_t count;
    do {
        count = write(STDERR_FILENO, &output_byte, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 1)
        console_failure("write");
}

/* The host is headless: both original text-console choices have the same
 * terminal endpoint. This does not supply an iOS framebuffer/IOSurface.
 */
void
vcputc_options(char c, bool poll)
{
    serial_putc_options(c, poll);
}

int
serial_getc(void)
{
    input_poll.fd = STDIN_FILENO;
    input_poll.events = POLLIN;
    input_poll.revents = 0;
    int ready = poll(&input_poll, 1, 0);
    if (ready < 0) {
        if (errno == EINTR)
            return -1;
        console_failure("poll");
    }
    if (input_poll.revents & (POLLERR | POLLNVAL)) {
        errno = EIO;
        console_failure("input descriptor");
    }
    if (!(input_poll.revents & POLLIN))
        return -1;
    ssize_t count = read(STDIN_FILENO, &input_byte, 1);
    if (count < 0) {
        if (errno == EINTR || errno == EAGAIN)
            return -1;
        console_failure("read");
    }
    return count == 1 ? input_byte : -1;
}

int
PE_stub_poll_input(unsigned int options, char *c)
{
    (void)options;
    int value = serial_getc();
    if (value < 0)
        return 1;
    *c = (char)value;
    return 0;
}
