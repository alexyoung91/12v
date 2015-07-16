#include <assert.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include "mcp3424.h"

void mcp3424_init(mcp3424 *m, int fd, uint8_t addr, enum mcp3424_bit_rate rate) {
	m->fd = fd;
	m->addr = addr;

	m->pga = 0.5f;
	m->lsb = 0.0000078125f;

	m->err = MCP3424_OK;

	mcp3424_set_bit_rate(m, rate);
	mcp3424_set_conversion_mode(m, MCP3424_CONVERSION_MODE_ONE_SHOT);
}

void mcp3424_set_bit_rate(mcp3424 *m, enum mcp3424_bit_rate rate) {
	assert(rate >= MCP3424_BIT_RATE_12 || rate <= MCP3424_BIT_RATE_18);
	//
}

void mcp3424_set_conversion_mode(mcp3424 *m, enum mcp3424_conversion_mode mode) {
	assert(mode == MCP3424_CONVERSION_MODE_ONE_SHOT || mode == MCP3424_CONVERSION_MODE_CONTINUOUS);
	//
}

unsigned int mcp3424_get_raw(mcp3424 *m, enum mcp3424_channel channel) {
	int rv;
	unsigned int raw;

	rv = ioctl(m->fd, I2C_SLAVE, m->addr);
	if (rv == -1) {
		snprintf(m->errstr, MCP3424_ERR_LEN, "ioctl: %s", strerror(errno));
		m->err = MCP3424_ERR;
		goto done;
	}

	raw = 0;

	//i2c_smbus_read_word_data

done:
	return raw;
}
