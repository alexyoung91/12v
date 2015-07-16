#include <assert.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include "mcp3424.h"

static void mcp3424_set_channel(mcp3424 *m, enum mcp3424_channel channel) {
	m->config &= ~0x60;
	m->config |= (channel << 5);
}

void mcp3424_init(mcp3424 *m, int fd, uint8_t addr, enum mcp3424_bit_rate rate) {
	m->fd = fd;
	m->addr = addr;
	m->config = 0x00;
	m->err = MCP3424_OK;
	mcp3424_set_bit_rate(m, rate);
	mcp3424_set_conversion_mode(m, MCP3424_CONVERSION_MODE_ONE_SHOT);
}

void mcp3424_set_bit_rate(mcp3424 *m, enum mcp3424_bit_rate rate) {
	m->config &= ~0x0c;
	m->config |= (rate << 2);
}

void mcp3424_set_conversion_mode(mcp3424 *m, enum mcp3424_conversion_mode mode) {
	m->config &= ~0x10;
	m->config |= (mode << 4);
}

void mcp3424_set_pga(mcp3424 *m, enum mcp3424_pga pga) {
	m->config &= ~0x03;
	m->config |= pga;
}

enum mcp3424_bit_rate mcp3424_get_bit_rate(mcp3424 *m) {
	return (m->config >> 2) & 0x03;
}

enum mcp3424_conversion_mode mcp3424_get_conversion_mode(mcp3424 *m) {
	return (m->config >> 4) & 0x03;
}

enum mcp3424_pga mcp3424_get_pga(mcp3424 *m) {
	return m->config & 0x03;
}

unsigned int mcp3424_get_raw(mcp3424 *m, enum mcp3424_channel channel) {
	int rv, n;
	unsigned int raw;

	mcp3424_set_channel(m, channel);

	rv = ioctl(m->fd, I2C_SLAVE, m->addr); // (need root permissions i believe)
	if (rv == -1) {
		snprintf(m->errstr, MCP3424_ERR_LEN, "ioctl: %s", strerror(errno));
		m->err = MCP3424_ERR;
		return 0;
	}

	/*
	* if the conversion mode is set to one-shot, write config with ready bit
	* set to 1
	*/
	if (mcp3424_get_conversion_mode(m) == MCP3424_CONVERSION_MODE_ONE_SHOT) {
		rv = i2c_smbus_write_byte(m->fd, m->config | (1 << 7));
		if (rv == -1) {
			snprintf(m->errstr, MCP3424_ERR_LEN, "i2c_smbus_write_byte: %s", strerror(errno));
			m->err = MCP3424_ERR;
			return 0;
		}
	}

	n = 0;
	do {
		rv = i2c_smbus_read_block_data(m->fd, m->config, m->reading + n);
		if (rv == -1) {
			snprintf(m->errstr, MCP3424_ERR_LEN, "i2c_smbus_read_block_data: %s", strerror(errno));
			m->err = MCP3424_ERR;
			return 0;
		}
		n += rv;
	} while (n < 4);

	switch (mcp3424_get_bit_rate(m)) {
		case MCP3424_BIT_RATE_12:
			raw = ((m->reading[0] & 0x0f) << 8) | m->reading[1];
			break;
		case MCP3424_BIT_RATE_14:
			raw = ((m->reading[0] & 0x3f) << 8) | m->reading[1];
			break;
		case MCP3424_BIT_RATE_16:
			raw = (m->reading[0] << 8) | m->reading[1];
			break;
		case MCP3424_BIT_RATE_18:
			raw = ((m->reading[0] & 0x03) << 16) | (m->reading[1] << 8) | m->reading[2];
			break;
		default:
			snprintf(m->errstr, MCP3424_ERR_LEN, "invalid bit rate");
			m->err = MCP3424_ERR;
			return 0;
	}

	return raw;
}
