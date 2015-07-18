#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <pcre.h>
#include "gusts.h"

#define WEATHER_URL "http://www.metoffice.gov.uk/public/weather/forecast/gcrvrkmyeyc4"
#define EXPRESSION "<span class=\"gust\"\ndata-type=\"windGust\" data-unit=\"(.*?)\"\ndata-mph=\"(.*?)\""

typedef struct {
	char *str;
	size_t len;
	size_t capacity;
} page;

static void gusts_set_err(gusts_results *gr, const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	vsnprintf(gr->errstr, GUSTS_ERR_LEN, format, ap);
	va_end(ap);
}

static size_t gusts_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
	page *p;
	char *tmp;
	size_t len;

	p = userdata;
	len = size * nmemb;

	while (p->capacity < p->len + len) {
		p->capacity *= 2;
		tmp = realloc(p->str, p->capacity);
		if (!tmp) {
			// handle error
		}
		p->str = tmp;
	}

	memcpy(p->str + p->len, ptr, len);
	p->len += len;

	printf("gusts_write_cb: p->len = %zu, p->capacity = %zu\n", p->len, p->capacity);
	//printf("ptr: %s\n", ptr);

	return len;
}

// http://www.mitchr.me/SS/exampleCode/AUPG/pcre_example.c.html
static void *gusts_worker(void *data) {
	gusts_results *gr;
	CURL *curl;
	CURLcode res;
	page p;

	char *aStrRegex;
	pcre *reCompiled;
	const char *pcreErrorStr;
	int pcreErrorOffset;
	pcre_extra *pcreExtra;
	int pcreExecRet;
  int subStrVec[30];
  const char *psubStrMatchStr;
  int j;

	gr = data;

	p.len = 0;
	p.capacity = 1024 * sizeof (char);
	p.str = malloc(p.capacity);
	if (!p.str) {
		gr->err = GUSTS_ERR;
		gusts_set_err(gr, "failed to allocate memory for web page data");
		pthread_exit(NULL);
	}

	curl = curl_easy_init();
	if (!curl) {
		gr->err = GUSTS_ERR;
		gusts_set_err(gr, "failed to init curl");
		pthread_exit(NULL);
	}

	curl_easy_setopt(curl, CURLOPT_URL, WEATHER_URL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, gusts_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &p);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		gr->err = GUSTS_ERR;
		gusts_set_err(gr, "curl_easy_perform: %s", curl_easy_strerror(res));
		pthread_exit(NULL);
	}
	curl_easy_cleanup(curl);

	//printf("%s\n", p.str);

	aStrRegex = EXPRESSION;

	reCompiled = pcre_compile(aStrRegex, 0, &pcreErrorStr, &pcreErrorOffset, NULL);
	if(!reCompiled) {
	    printf("ERROR: Could not compile '%s': %s\n", aStrRegex, pcreErrorStr);
	    exit(1);
	} /* end if */

	pcreExtra = pcre_study(reCompiled, 0, &pcreErrorStr);

	if(pcreErrorStr != NULL) {
    printf("ERROR: Could not study '%s': %s\n", aStrRegex, pcreErrorStr);
    exit(1);
  } /* end if */

  pcreExecRet = pcre_exec(reCompiled,
                            pcreExtra,
                            p.str,
                            strlen(p.str),  // length of string
                            0,                      // Start looking at this point
                            0,                      // OPTIONS
                            subStrVec,
                            30);                    // Length of subStrVec

	printf("pcreExecRet: %d\n", pcreExecRet);

	// PCRE contains a handy function to do the above for you:
      for(j=0; j<pcreExecRet; j++) {
        pcre_get_substring(p.str, subStrVec, pcreExecRet, j, &(psubStrMatchStr));
        printf("Match(%2d/%2d): (%2d,%2d): '%s'\n", j, pcreExecRet-1, subStrVec[j*2], subStrVec[j*2+1], psubStrMatchStr);
      } /* end for */

      // Free up the substring
      pcre_free_substring(psubStrMatchStr);

	// Free up the regular expression.
  pcre_free(reCompiled);

  // Free up the EXTRA PCRE value (may be NULL at this point)
  if(pcreExtra != NULL)
    pcre_free(pcreExtra);

	gr->err = GUSTS_OK;

	pthread_exit(NULL);
}

int gusts_get(gusts_results *gr) {
	int rv;
	pthread_t thread;

	rv = pthread_create(&thread, NULL, gusts_worker, (void *)gr);

	pthread_join(thread, NULL);

	return GUSTS_OK;
}
