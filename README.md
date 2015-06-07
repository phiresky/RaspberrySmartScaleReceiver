Program to receive the body weight and body composition analysis data from a Soehnle 63760 BB smart scale using a 433 Mhz receiver on the Raspberry Pi.

## How it works

This program runs in the background, reading from the reciever for a few milliseconds every second to watch for a valid data packet. It then listens for data continously until the body analysis is complete. Meanwhile it uses speech synthesis to tell you how fat you are.

