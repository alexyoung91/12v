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

#define STUBBED(str) printf("STUBBED: %s\n", str)
#define MAP(val, from_min, from_max, to_min, to_max) \
	((val - from_min) * ((to_max - to_min) / (from_max - from_min))) + to_min

/* ====== Configuration values ====== */

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

static void sig_handler(int sig, siginfo_t *siginfo, void *context);
const char *get_bus_id(void);
void use_battery(void);
void use_mains(void);
void display(void);

/* ====== Globals ====== */

static int running;
static system_12v sys;
static wind_turbine wt;
static solar_panel sp;
static battery bat;

/* ====== Entry ====== */

int main(int argc, char **argv) {
	int rv;
	struct sigaction act;
	int fd;
	const char *bus;
	mcp3424 m;
	unsigned int raw;

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
	//bus = get_bus_id();
	STUBBED("get_bus_id (for testing on non rpi)");
	bus = NULL;
	if (!bus) {
		printf("error: could not determine bus id\n");
		return EXIT_FAILURE;
	}

	fd = open(bus, O_RDWR);

	if (fd == -1) {
		printf("error: could not open %s: %s\n", bus, strerror(errno));
		return EXIT_FAILURE;
	}

	mcp3424_init(&m, fd, 0x68, MCP3424_BIT_RATE_14);
	if (m.err == MCP3424_ERR) {
		printf("error: mcp3424_init: %s\n", m.errstr);
		return EXIT_FAILURE;
	}

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

	memset(&wt, 0, sizeof (wind_turbine));
	memset(&sp, 0, sizeof (solar_panel));
	memset(&bat, 0, sizeof (battery));

	initscr();

	running = 1;
	while (running) {
		/*
		* read battery voltage from MCP3424 ADC
		*/
		raw = mcp3424_get_raw(&m, MCP3424_CHANNEL_1);
		bat.v = MAP(raw, 0, 9999, 0.0, 16.0);

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

static void sig_handler(int sig, siginfo_t *siginfo, void *context) {
	if (sig == SIGINT) {
		running = 0;
	}
}

/*
* Returns I2C bus depending on raspberry pi revision
*/
const char *get_bus_id(void) {
	FILE *fp;
	char c;
	char buf[1024];
	char rev[32];
	int pos = 0;
	int n;
	const char *bus = NULL;

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
				buf[pos++] = c;
				if (pos == sizeof (buf)) {
					// line too big ya'll
					return bus;
				}
			}
		} while (c != EOF && c != '\n');
		n = sscanf(buf, "Revision : %s", rev);
		if (n == 1) {
			if (strcmp(rev, "0002") == 0 || (strcmp(rev, "0003") == 0)) {
				bus = "/dev/i2c-0";
			} else {
				bus = "/dev/i2c-1";
			}
		}
	} while (c != EOF);

	fclose(fp);

	return bus;
}

void use_battery(void) {
	sys.source = SOURCE_BATTERY;
	bcm2835_gpio_write(SOURCE_RELAY_PIN, HIGH);
}

void use_mains(void) {
	sys.source = SOURCE_MAINS;
	bcm2835_gpio_write(SOURCE_RELAY_PIN, LOW);
}

void display(void) {
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
