#include "devmemgpio.h"
#include <cstdint>
#include <sys/time.h>
#include <vector>
#include <fstream>
#include <iostream>
using namespace std;

using byte = unsigned char;
// timing code adapted from wiringPi

uint64_t freq = 44100; // target read frequency
// minimum pulse width to interpret bit as 1 (25 samples)
int yeslen_us = 560; 
int min_controllen_us = 900; // minimum control bit length (40 samples)
int max_controllen_us = 1800; // maximum control bit length (80 samples)
uint64_t iteration_us = 1500000;
uint64_t capture_us =   100000;

bool voice = true;
bool outputFiltered = false;

void weight_callback(double weight) {
	if(!voice) return;
	char buf[100];
	snprintf(buf,100,"say %.1f kilogram &", weight);
	system(buf);
}

int yes_samples = yeslen_us*freq/1000000;
int min_control_samples = min_controllen_us*freq/1000000;
int max_control_samples = max_controllen_us*freq/1000000;

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

void receive(int rate_hz, byte* output, int sample_count) {
	timeval delay = {0, int(1000000.0/rate_hz - 1 + 0.5)};
	timeval time; gettimeofday(&time, NULL);
	for(int i=0;i<sample_count;i++) {
		output[i] = GET_GPIO(18)?255:0;
		busy_wait(&time, &delay);
	}
}

uint64_t tobytes(vector<byte> bits, int start, int len) {
	uint64_t out = 0;
	for(int i = start; i < start + len; i++)
		out = (out<<1) + bits.at(i);
	return out;
}

bool parsepacket(vector<byte> bits) {
	if(bits.size() != 6*8) return false;
	uint64_t type = tobytes(bits, 0, 3*8+5) << 3;
	if(type == 0x00100020) {
		double weight = tobytes(bits, 3*8+4, 12)/10.0;
		fprintf(stderr,"weight: %.1f", weight);
		weight_callback(weight);
	} else {
		cerr << "unknown message ";
	}
	cerr << "(";
	for(byte c:bits) cerr << (c?'1':'0');
	cerr << ")\n";
	return true;
}


vector<byte> current_bits;
bool running = false;
bool next_pulse(bool high, int length) {
	if(!high) {
		if(running && length > yes_samples) {
			bool valid = parsepacket(current_bits);
			running = false;
			current_bits.clear();
			return valid;
		}
	} else {
		if(length > min_control_samples) {
			if(running) {
				// parsepacket(current_bits);
				current_bits.clear();
			}
			running = length < max_control_samples;
		} else if(running) {
			current_bits.push_back(length < yes_samples ? 0 : 1);
		}
	}
	return false;
}
void analyze(byte* cache, int cache_size) {
	bool state = false;
	int curval = 0;
	int length = 0;
	int lastPacketStart = 0;
	for(int i=0;i<cache_size;i++) {
		// ignore flips shorter than ~2 samples
		curval = int((cache[i]+3*curval)/4); 
		if((curval>127) != state) {
			bool running_before = running;
			bool received = next_pulse(state, length);
			if(!running_before && running && outputFiltered) {
				for(;lastPacketStart < i;lastPacketStart++) {
					putchar(127);
				}
			}
			if(received && outputFiltered) {
				for(;lastPacketStart < i - length; lastPacketStart++) {
					putchar(cache[lastPacketStart]);
				}
			}
			state = curval > 127;
			length = 1;
		} else length++;
	}
	
	if(current_bits.size() > 0) parsepacket(current_bits);
}

pair<byte*,int> readfile(string name) {
	ifstream is(name, ifstream::binary);
	is.seekg(0, is.end);
	int length = is.tellg();
	is.seekg(0, is.beg);
	byte* buffer = new byte[length];
	is.read((char*)buffer, length);
	return {buffer,length};
}

int main(int argc, char* argv[]) {
	if(argc>1) {
		voice = false;
		outputFiltered = true;
		auto x = readfile(argv[1]);
		analyze(x.first,x.second);
		return 0;
	}
	setup_io();
	INP_GPIO(18);
	uint32_t cache_size = freq * capture_us / 1000000;
	byte cache[cache_size];

	fprintf(stderr,"reading %u samples per second (%.1f%%)\n",
			cache_size,100.0*capture_us/iteration_us);

	// waage has 2.2s initialization signal, 
	// so capture 0.07s every 1s to detect it

	while(true) {
		uint64_t bef = micros();
		receive(freq, cache, cache_size);
		ssize_t wlen = write(1, cache, cache_size);
		if(wlen != cache_size) exit(-1);
		analyze(cache, cache_size);
		usleep(iteration_us - (micros()-bef));
	}
}

