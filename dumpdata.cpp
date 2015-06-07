// dumps a raw data stream from the GPIO pin of the
// 433MHz receiver for later analysis / reverse engineering

#include "devmemgpio.h"
#include <cstdint>
#include <sys/time.h>
#include <iostream>
#include <signal.h>
using namespace std;

uint64_t freq = 44100; // target read frequency


void busy_wait(timeval* time, timeval* delay) {
	timeradd(time, delay, time);

	timeval now;
	do {
		gettimeofday(&now, NULL);
	} while (timercmp(&now, time, <));
}
uint64_t micros() {
	struct timeval tv;
	gettimeofday (&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

uint64_t bef;
uint64_t i = 0;
void handleExit(int s) {
	cerr << "dumped " << i << " samples" << endl;
	double rate = ((double)micros()-bef)/i;
	fprintf(stderr, "reading rate: %.2f (%.1f kHz)",rate,1000/rate);
	exit(0);
}

int main(int argc, char* argv[]) {
	struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = handleExit;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);

	setup_io();
	INP_GPIO(18);

	bef = micros();
	timeval time; gettimeofday(&time, NULL);
	timeval delay = {0, int(1000000.0/freq + 0.5)};
	while(true) {
		putchar(GET_GPIO(18)?255:0);
		busy_wait(&time, &delay);
		i++;
	}
}

