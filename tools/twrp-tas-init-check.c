/* SPDX-License-Identifier: MIT
 * Temporary recovery-only harness for the unpowered TAS2557 transcript.
 * The caller must establish exclusive access, verify this phone's transcript,
 * hold/release hardware reset through the stock driver before running, and
 * restore stock driver state afterwards. It is not an iOS playback driver.
 * I2C_RDWR reference: https://docs.kernel.org/i2c/dev-interface.html
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>
#include "mi8-tas-init.h"

struct context { int fd, delay_failed; };
static int bus_transfer(void *context, uint8_t address, const uint8_t *out, size_t n, uint8_t *in, size_t m)
{
	struct context *c = context;
	if (c->delay_failed) return -1;
	struct i2c_msg msgs[2] = {
		{ .addr = address, .flags = 0, .len = (uint16_t)n, .buf = (uint8_t *)(uintptr_t)out },
		{ .addr = address, .flags = I2C_M_RD, .len = (uint16_t)m, .buf = in }
	};
	struct i2c_rdwr_ioctl_data data = { .msgs = msgs, .nmsgs = m ? 2u : 1u };
	return ioctl(c->fd, I2C_RDWR, &data) == (int)data.nmsgs ? 0 : -1;
}
static void delay_us(void *context, uint32_t us)
{
	struct timespec t = { .tv_sec = us / 1000000, .tv_nsec = (long)(us % 1000000) * 1000 };
	struct context *c = context;
	while (nanosleep(&t, &t)) {
		if (errno != EINTR) { c->delay_failed = 1; return; }
	}
}
int main(int argc, char **argv)
{
	struct stat st;
	struct context c = { .fd = -1, .delay_failed = 0 };
	struct mi8_tas_init_io io = { &c, bus_transfer, delay_us };
	uint8_t *bytes;
	uint32_t failed;
	FILE *source;
	size_t length;
	int result;
	char resolved[PATH_MAX];
	if (argc != 2 || !(source = fopen(argv[1], "rb")) || fstat(fileno(source), &st) ||
	    !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 1000000) return 2;
	length = (size_t)st.st_size;
	bytes = malloc(length);
	if (!bytes || fread(bytes, 1, length, source) != length || fclose(source)) return 2;
	if (mi8_tas_init_validate(bytes, length)) { free(bytes); return 3; }
	if (!realpath("/sys/bus/i2c/devices/2-004c", resolved) ||
	    !strstr(resolved, "/894000.i2c/i2c-2/2-004c")) { free(bytes); return 4; }
	c.fd = open("/dev/i2c-2", O_RDWR | O_CLOEXEC);
	if (c.fd < 0 || fstat(c.fd, &st) || !S_ISCHR(st.st_mode) || major(st.st_rdev) != 89 || minor(st.st_rdev) != 2) {
		free(bytes); return 4;
	}
	result = mi8_tas_init_run(bytes, length, &io, &failed);
	printf("result=%d failed_record=%u\n", result, failed);
	close(c.fd); free(bytes);
	return result ? 5 : 0;
}
