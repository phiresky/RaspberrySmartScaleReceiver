all: waagereader

waagereader: waagereader.cpp devmemgpio.h
	g++ -std=c++11 -O3 waagereader.cpp -o waagereader

dumpdata: dumpdata.cpp devmemgpio.h
	g++ -std=c++11 -O3 dumpdata.cpp -o dumpdata
