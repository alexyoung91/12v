#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <ncurses.h>
#include <bcm2835.h>
#include "mcp3424.h"

#define MAP(val, from_min, from_max, to_min, to_max) \
	((val - from_min) * ((to_max - to_min) / (from_max - from_min))) + to_min

/* ====== Configuration values ====== */

#define ADCV_ADDR 0x68 // voltage
#define ADCI_ADDR 0x69 // current

#define BAT_LOW_V 12.0f // v
#define BAT_LOW_V_HYS 1.0f // change in v

/* ====== GPIO ====== */

#define BATTERY_VOLTAGE_PIN 10
#define SOURCE_RELAY_PIN 11

/* ====== Structures ====== */

typedef struct {
	enum {
		SOURCE_BATTERY,
		SOURCE_MAINS
	} source;
} system_12v;

typedef struct {
	float v;
	float i;
	int braked;
} wind_turbine;

typedef struct {
	float v;
	float i;
} solar_panel;

typedef struct {
	float v;
} battery;

/* ====== Prototypes ====== */

static void read_battery_state(battery *bat);
static void read_wind_turbine_state(wind_turbine *wt);
static void read_solar_panel_state(solar_panel *sp);
static void sig_handler(int sig, siginfo_t *siginfo, void *context);
static int get_rpi_revision(char *rev);
static void use_battery(void);
static void use_mains(void);
static void display(void);

/* ====== Globals ====== */

static mcp3424 adcv, adci;
static int running;
static system_12v sys;
static battery bat;
static wind_turbine wt;
static solar_panel sp;

/* ====== Entry ====== */

int main(int argc, char **argv) {
	int rv;
	char rev[32];
	const char *filename;
	int fd;
	struct sigaction act;

	/* ====== BCM2835 ====== */

	// with debug set GPIO is not actually used
	bcm2835_set_debug(1);

	printf("initialising bcm2835...\n");
	rv = bcm2835_init();
	if (!rv) {
		printf("error: could not initialise bcm2835\n");
		return EXIT_FAILURE;
	}

	bcm2835_gpio_fsel(SOURCE_RELAY_PIN, BCM2835_GPIO_FSEL_OUTP);

	/* ====== MCP3424 ====== */

	printf("initialising mcp3424...\n");
	rv = get_rpi_revision(rev);
	if (rv == 0) {
		printf("error: could not determine raspberry pi revision\n");
		return EXIT_FAILURE;
	}
	if (strcmp(rev, "0002") == 0 || strcmp(rev, "0003") == 0) {
		filename = "/dev/i2c-0";
	} else {
		filename = "/dev/i2c-1";
	}

	fd = open(filename, O_RDWR);
	if (fd == -1) {
		printf("error: could not open %s: %s\n", filename, strerror(errno));
		return EXIT_FAILURE;
	}

	mcp3424_init(&adcv, fd, ADCV_ADDR, MCP3424_BIT_RATE_14);
	mcp3424_init(&adci, fd, ADCI_ADDR, MCP3424_BIT_RATE_14);

	/* ====== Interrupt handling ====== */

	memset(&act, 0, sizeof (act));
	act.sa_sigaction = &sig_handler;
	act.sa_flags = SA_SIGINFO;

	printf("registering signal handler...\n");
	rv = sigaction(SIGINT, &act, NULL);
	if (rv == -1) {
		printf("error: sigaction: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	memset(&sys, 0, sizeof (system_12v));
	sys.source = SOURCE_BATTERY;

	memset(&bat, 0, sizeof (battery));
	memset(&wt, 0, sizeof (wind_turbine));
	memset(&sp, 0, sizeof (solar_panel));

	initscr();

	running = 1;
	while (running) {
		read_battery_state(&bat);
		read_wind_turbine_state(&wt);
		read_solar_panel_state(&sp);

		/*
		* compare battery voltage with constant to determine whether system
		* should be powered by mains or by battery
		*/
		if (sys.source == SOURCE_BATTERY) {
			if (bat.v <= BAT_LOW_V) {
				use_mains();
			} else {
				use_battery();
			}
		} else {
			if (bat.v >= BAT_LOW_V + BAT_LOW_V_HYS) {
				use_battery();
			} else {
				use_mains();
			}
		}

		display();

		bcm2835_delay(500);
	}

	endwin();

	printf("closing mcp3424...\n");
	close(fd);

	printf("closing bcm2835...\n");
	bcm2835_close();

	return EXIT_SUCCESS;
}

static void read_battery_state(battery *bat) {
	unsigned int raw;

	raw = mcp3424_get_raw(&adcv, MCP3424_CHANNEL_1);
	if (adcv.err == MCP3424_ERR) {
		printf("error: mcp3424_get_raw: %s\n", adcv.errstr);
		exit(EXIT_FAILURE);
	}
	bat->v = MAP(raw, 0, 9999, 0.0, 16.0);
}

static void read_wind_turbine_state(wind_turbine *wt) {
	unsigned int raw;

	raw = mcp3424_get_raw(&adcv, MCP3424_CHANNEL_2);
	if (adcv.err == MCP3424_ERR) {
		goto error;
	}
	wt->v = MAP(raw, 0, 9999, 0.0, 16.0);

	raw = mcp3424_get_raw(&adci, MCP3424_CHANNEL_2);
	if (adcv.err == MCP3424_ERR) {
		goto error;
	}
	wt->i = MAP(raw, 0, 9999, 0.0, 16.0);

	return;

error:
	printf("error: mcp3424_get_raw: %s\n", adcv.errstr);
	exit(EXIT_FAILURE);
}

static void read_solar_panel_state(solar_panel *sp) {
	unsigned int raw;

	raw = mcp3424_get_raw(&adcv, MCP3424_CHANNEL_3);
	if (adcv.err == MCP3424_ERR) {
		goto error;
	}
	sp->v = MAP(raw, 0, 9999, 0.0, 16.0);

	raw = mcp3424_get_raw(&adci, MCP3424_CHANNEL_3);
	if (adcv.err == MCP3424_ERR) {
		goto error;
	}
	sp->i = MAP(raw, 0, 9999, 0.0, 16.0);

	return;

error:
	printf("error: mcp3424_get_raw: %s\n", adcv.errstr);
	exit(EXIT_FAILURE);
}

static void sig_handler(int sig, siginfo_t *siginfo, void *context) {
	if (sig == SIGINT) {
		running = 0;
	}
}

/*
* Returns RPi revision from /proc/cpuinfo
*/
static int get_rpi_revision(char *rev) {
	FILE *fp;
	char c;
	char buf[1024];
	int pos = 0;
	int n;

	fp = fopen("/proc/cpuinfo", "r");
	if (!fp) {
		printf("err: could not open \"/proc/cpuinfo\"\n");
		exit(EXIT_FAILURE);
	}

	do {
		pos = 0;
		do {
			c = fgetc(fp);
			if (c != EOF && c != '\n') {
				if (pos < sizeof (buf)) {
					buf[pos++] = c;
				} else {
					continue;
				}
			}
		} while (c != EOF && c != '\n');
		n = sscanf(buf, "Revision : %s", rev);
		if (n == 1) {
			break;
		}
	} while (c != EOF);

	fclose(fp);

	return n;
}

static void use_battery(void) {
	sys.source = SOURCE_BATTERY;
	bcm2835_gpio_write(SOURCE_RELAY_PIN, HIGH);
}

static void use_mains(void) {
	sys.source = SOURCE_MAINS;
	bcm2835_gpio_write(SOURCE_RELAY_PIN, LOW);
}

static void display(void) {
	printw("System");
	/*printf("======\n");
	printf("Source (S):\t\t%s\n", sys.source == SOURCE_BATTERY ? "Battery" : "Mains");
	printf("\n");

	printf("Wind Turbine\n");
	printf("============\n");
	printf("Voltage (V) / V:\t%.2f\n", wt.v);
	printf("Current (I) / A:\t%.2f\n", wt.v);
	printf("Braked (B):\t\t%s\n", wt.braked == 0 ? "No" : "Yes");
	printf("\n");

	printf("Solar Panel\n");
	printf("===========\n");
	printf("Voltage (V) / V:\t%.2f\n", sp.v);
	printf("Current (I) / A:\t%.2f\n", sp.v);
	printf("\n");

	printf("Battery\n");
	printf("=======\n");
	printf("Voltage (V) / V:\t%.2f\n", bat.v);
	printf("\n");*/

	refresh();
}
