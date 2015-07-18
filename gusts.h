#ifndef GUSTS_H_
#define GUSTS_H_

#include <sys/time.h>

#define GUSTS_OK 0
#define GUSTS_ERR -1
#define GUSTS_ERR_LEN 256

#define HOUR_LEN 6 // 00:00 + null

typedef struct {;
	unsigned int res[24]; // 24 hours
	size_t len;
	int err;
	char errstr[GUSTS_ERR_LEN];
} gusts_results;

int gusts_get(gusts_results *gr);

#endif /* GUSTS_H_ */
