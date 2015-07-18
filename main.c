#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bcm2835.h>
#include <linux/i2c-dev.h>
#include <ncurses.h>
#include "mcp3424.h"

#define MAP(val, from_min, from_max, to_min, to_max) \
	((val - from_min) * ((to_max - to_min) / (from_max - from_min))) + to_min

/* ====== Configuration values ====== */

#define ADCV_ADDR 0x68 // voltage
#define ADCI_ADDR 0x69 // current

#define BAT_LOW_V 12.00f // v
#define BAT_LOW_V_HYS 2.0f // change in v

#define BATTERY_V_RAW_MIN 0
#define BATTERY_V_RAW_MAX 1860
#define BATTERY_V_MIN 0.0f
#define BATTERY_V_MAX 12.98f

/* ====== GPIO ====== */

#define BATTERY_VOLTAGE_PIN 10
#define SOURCE_RELAY_PIN RPI_GPIO_P1_07

/* ====== Structures ====== */

typedef struct {
	enum {
		SOURCE_BATTERY,
		SOURCE_MAINS
	} source;
} system12v;

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

typedef struct {
	float i;
} load;

/* ====== Prototypes ====== */

static void initialise(void);
static int get_rpi_revision(char *rev);
static void sig_handler(int sig, siginfo_t *siginfo, void *context);
static WINDOW *window_create(int x, int y, int width, int height);

static void read_battery_state(battery *bat);
static void read_wind_turbine_state(wind_turbine *wt);
static void read_solar_panel_state(solar_panel *sp);
static void use_battery(void);
static void use_mains(void);
static void display_measurements(void);
static void display_status(void);

static void window_destroy(WINDOW *win);
static void quit(void);

/* ====== Globals ====== */

static int fd;
static mcp3424 adcv, adci;
static WINDOW *win1; // title
static WINDOW *win2; // measurements
static WINDOW *win3; // status

static system12v sys;
static battery bat;
static wind_turbine wt;
static solar_panel sp;
static int running = 1;
static unsigned int it = 0;

/* ====== Entry ====== */

int main(int argc, char **argv) {
	initialise();

	memset(&sys, 0, sizeof (system12v));
	memset(&bat, 0, sizeof (battery));
	memset(&wt, 0, sizeof (wind_turbine));
	memset(&sp, 0, sizeof (solar_panel));

	sys.source = SOURCE_BATTERY;

	while (running) {
		read_battery_state(&bat);
		//read_wind_turbine_state(&wt);
		//read_solar_panel_state(&sp);

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
		
		//display_measurements();
		//display_status();
		bcm2835_delay(100);

		it++;
	}

	quit();

	return EXIT_SUCCESS;
}

static void initialise(void) {
	int rv;
	char rev[32];
	const char *filename;
	struct sigaction act;

	/* ====== BCM2835 ====== */

	// with debug set to 1 GPIO is not actually used
	bcm2835_set_debug(0);

	printf("initialising bcm2835...\n");
	rv = bcm2835_init();
	if (!rv) {
		// bcm2835_init prints error message
		exit(EXIT_FAILURE);
	}

	// set SOURCE_RELAY_PIN to output
	bcm2835_gpio_fsel(SOURCE_RELAY_PIN, BCM2835_GPIO_FSEL_OUTP);

	/* ====== MCP3424 ====== */

	printf("initialising mcp3424...\n");
	rv = get_rpi_revision(rev);
	if (rv == 0) {
		printf("error: could not determine raspberry pi revision\n");
		exit(EXIT_FAILURE);
	}
	if (strcmp(rev, "0002") == 0 || strcmp(rev, "0003") == 0) {
		filename = "/dev/i2c-0";
	} else {
		filename = "/dev/i2c-1";
	}

	fd = open(filename, O_RDWR);
	if (fd == -1) {
		printf("error: could not open %s: %s\n", filename, strerror(errno));
		exit(EXIT_FAILURE);
	}

	mcp3424_init(&adcv, fd, ADCV_ADDR, MCP3424_BIT_RATE_16);
	mcp3424_set_conversion_mode(&adcv, MCP3424_CONVERSION_MODE_CONTINUOUS);
	//mcp3424_init(&adci, fd, ADCI_ADDR, MCP3424_BIT_RATE_14);

	/* ====== Signal handling ====== */

	memset(&act, 0, sizeof (act));
	act.sa_sigaction = &sig_handler;
	act.sa_flags = SA_SIGINFO;

	printf("registering signal handler...\n");
	rv = sigaction(SIGINT, &act, NULL);
	if (rv == -1) {
		printf("error: sigaction: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	rv = sigaction(SIGWINCH, &act, NULL);
	if (rv == -1) {
		printf("error: sigaction: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* ====== ncurses ====== */
/*
	initscr(); // start ncurses
	cbreak(); //
	noecho();
	keypad(stdscr, TRUE);
	win1 = window_create(0, 0, 120, 4);
	win2 = window_create(0, 4, 80, 26);
	win3 = window_create(80, 4, 40, 26);

	wmove(win1, 1, 1);
	wprintw(win1, "12v");
	wrefresh(win1);
*/
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
		n = sscanf(buf, "Revision : %s\n", rev);
		if (n == 0) {
			continue;
		} else {
			rev[4] = '\0';
			break;
		}
	} while (c != EOF);

	fclose(fp);

	return n;
}

static void sig_handler(int sig, siginfo_t *siginfo, void *context) {
	switch (sig) {
		case SIGINT:
			running = 0;
			break;
		case SIGWINCH:
			printf("window resized\n");
			break;
		default:
			break;
	}
}

static void quit(void) {
/*	window_destroy(win3);
	window_destroy(win2);
	window_destroy(win1);
	endwin();
*/
	printf("closing mcp3424...\n");
	close(fd);

	printf("closing bcm2835...\n");
	bcm2835_close();
}

static WINDOW *window_create(int x, int y, int width, int height) {
	WINDOW *win;

	win = newwin(height, width, y, x);
	wborder(win, 0, 0, 0, 0, 0, 0, 0, 0);
	wrefresh(win);

	return win;
}

static void window_destroy(WINDOW *win) {
	delwin(win);
}

static void read_battery_state(battery *bat) {
	unsigned int raw;
/*
	static unsigned int avg = 0;
	static float avg2 = 0.0f;
	static unsigned int i = 1;
*/
	raw = mcp3424_get_raw(&adcv, MCP3424_CHANNEL_1);
	if (adcv.err == MCP3424_ERR) {
		printf("error: mcp3424_get_raw: %s\n", adcv.errstr);
		exit(EXIT_FAILURE);
	}

	//bat->v = MAP(raw, BATTERY_V_RAW_MIN, BATTERY_V_RAW_MAX, BATTERY_V_MIN, BATTERY_V_MAX);
	bat->v = MAP(raw, 0, 32768, 0.0f, 59.8f);

	printf("%u - raw: %u, v: %0.2f\n", it, raw, bat->v);
/*
	avg += raw;
	avg2 += bat->v;
	i++;
	printf("raw = %u, bat->v = %0.2f, avg = %u, avg2 (v) = %0.2f\n", raw, bat->v, avg / i, avg2 / i);
*/
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

static void use_battery(void) {
	sys.source = SOURCE_BATTERY;
	bcm2835_gpio_write(SOURCE_RELAY_PIN, LOW);
	printf("using battery\n");
}

static void use_mains(void) {
	sys.source = SOURCE_MAINS;
	bcm2835_gpio_write(SOURCE_RELAY_PIN, HIGH);
	printf("using mains\n");
}

static void display_measurements(void) {
	wclear(win2);
	wmove(win2, 1, 1);
	wprintw(win2, "Measurements");
	wmove(win2, 2, 1);
	whline(win2, ACS_HLINE, 78);
	wmove(win2, 4, 0);

	wprintw(win2, " Wind Turbine\n");
	wprintw(win2, " ============\n");
	wprintw(win2, " Voltage (V) / V:\t%.2f\n", wt.v);
	wprintw(win2, " Current (I) / A:\t%.2f\n", wt.v);
	wprintw(win2, " Braked (B):\t\t%s\n", wt.braked == 0 ? "No" : "Yes");
	wprintw(win2, " \n");

	wprintw(win2, " Solar Panel\n");
	wprintw(win2, " ===========\n");
	wprintw(win2, " Voltage (V) / V:\t%.2f\n", sp.v);
	wprintw(win2, " Current (I) / A:\t%.2f\n", sp.v);
	wprintw(win2, " \n");

	wprintw(win2, " Battery\n");
	wprintw(win2, " =======\n");
	wprintw(win2, " Voltage (V) / V:\t%.2f\n", bat.v);
	wprintw(win2, " \n");

	wborder(win2, 0, 0, 0, 0, 0, 0, 0, 0);
	wrefresh(win2);
}

static void display_status(void) {
	wmove(win3, 1, 1);
	wprintw(win3, "Status");
	wmove(win3, 2, 1);
	whline(win3, ACS_HLINE, 38);
	wmove(win3, 4, 0);

	wprintw(win3, " System Source (S):\t%s\n", sys.source == SOURCE_BATTERY ? "Battery" : "Mains");
	wprintw(win3, " Iteration (it):\t%u\n", it);
	wprintw(win3, " \n");

	wborder(win3, 0, 0, 0, 0, 0, 0, 0, 0);
	wrefresh(win3);
}
