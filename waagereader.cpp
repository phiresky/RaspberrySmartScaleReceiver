#include "devmemgpio.h"
#include <cstdint>
#include <sys/time.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <cinttypes>
using namespace std;

#define DEBUG 0
using byte = unsigned char;

uint64_t rate_hz = 43500; // target read frequency
// minimum pulse width to interpret bit as 1 (25 samples)
int yeslen_us = 560; 
int min_controllen_us = 900; // minimum control bit length (40 samples)
int max_controllen_us = 1800; // maximum control bit length (80 samples)
uint64_t iteration_us = 1500000;
uint64_t capture_us =    150000;

uint64_t standby_timeout_us = 10*1000*1000; // time since last packet to go to standby mode

bool voice = true;

void weight_callback(double weight) {
	if(!voice) return;
	char buf[100];
	snprintf(buf,100,"say %.1f kilogram &", weight);
	system(buf);
}

int yes_samples = yeslen_us*rate_hz/1000000;
int min_control_samples = min_controllen_us*rate_hz/1000000;
int max_control_samples = max_controllen_us*rate_hz/1000000;

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

uint64_t tobytes(vector<byte>& bits, int start, int len, bool setdone=true) {
	uint64_t out = 0;
	for(int i = start; i < start + len; i++) {
		out = (out<<1) + bits.at(i);
		if(setdone) bits[i] = 2;
	}
	return out;
}

bool parsepacket(vector<byte>& bits) {
	if(bits.size() != 6*8) return false;
	if(bits[11]!=1||bits[12]!=0) return false;

	byte checksum = byte(tobytes(bits,5*8,8));
	(void) checksum;


	int imp5k = (int) tobytes(bits, 0, 11);
	int imp50k = (int) tobytes(bits,13,11);
	double weight = tobytes(bits, 29, 11)/10.0;
	bool s01 = bits[26];
	bool s02 = bits[28];
	bool s03 = bits[25];
	string statusString = "unknown   ";
	if( s01 && !s02 && !s03) statusString = "weighing   ";
	if(!s01 && !s02 && !s03) statusString = "finalweight";
	if(!s01 &&  s02 && !s03) statusString = "analyzing  ";
	if( s01 &&  s02 &&  s03) statusString = "complete   ";
	if( s01 && !s02 &&  s03) statusString = "error      ";
	fprintf(stderr,"%04.1f kg, imp5k=%03d, imp50k=%03d, %s", weight, imp5k, imp50k, statusString.c_str());
	cerr << "(";
	int i=0;
	for(byte c:bits) cerr << (c==2?'_':char(c+'0')) << ((++i%4)?"":" ");
	cerr << ")\n";
	return true;
}


bool next_pulse(bool high, int length) {
	static vector<byte> current_bits;
	static bool running = false;

	if(!high) {
		if(running && length > yes_samples) {
			bool valid = parsepacket(current_bits);
			running = false;
			current_bits.clear();
			return valid;
		}
	} else {
		if(length > min_control_samples) {
			if(running) // packet not correctly ended
				current_bits.clear();
			running = length < max_control_samples;
		} else if(running) {
			current_bits.push_back(length < yes_samples ? 0 : 1);
		}
	}
	return false;
}
bool analyze(byte val) {
	static bool state = false;
	static int curval = 0;
	static int length = 0;
	// ignore flips shorter than ~2 samples
	curval = int((val+3*curval)/4); 
	bool found_packet = false;
	if((curval>127) != state) {
		found_packet = next_pulse(state, length);
		state = curval > 127;
		length = 1;
	} else length++;
	return found_packet;
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

uint64_t last_found_packet = 0;

int main(int argc, char* argv[]) {
	if(argc>1) {
		voice = false;
		auto x = readfile(argv[1]);
		for(int i=0;i<x.second;i++)
			analyze(x.first[i]);
		return 0;
	}
	setup_io();
	INP_GPIO(18);

	uint32_t iteration_count = rate_hz * capture_us / 1000000;
	fprintf(stderr, "reading %.1f%% of the time\n", 100.0*capture_us/iteration_us);

	// waage has 2.2s initialization signal, 
	// so capture 0.07s every 1s to detect it

	timeval delay = {0, int(1000000.0/rate_hz - 1 + 0.5)};
	timeval time; gettimeofday(&time, NULL);
	int i = 0;
	uint64_t bef = micros();
	while(true) {
		byte val = GET_GPIO(18)?255:0;
		if(analyze(val)) {
			last_found_packet = micros();
		}
		putchar(val);
		busy_wait(&time, &delay);
		if(++i%iteration_count==0) {
			uint64_t now = micros();
			if((now-last_found_packet) > standby_timeout_us) {
				int sleepdur = iteration_us - (now-bef);
				if(DEBUG) cerr << "iteration complete, sleeping "<<sleepdur<<endl;
				usleep(sleepdur);
				gettimeofday(&time, NULL);
				bef = micros();
			} else bef = now;
		}
	}
}

