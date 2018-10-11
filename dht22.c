#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <pigpio.h>

#define STATE_END		0
#define STATE_START		1
#define STATE_DHT_RESPONDED	2
#define STATE_GET_READY 	3
#define STATE_BIT_LOW		4
#define STATE_BIT		5

#define DHT22PIN 4

uint8_t byte[5];
int nbit, nbyte;
uint32_t bitstart;
int running;

void
readbit(int gpio_pin, int gpio_level, uint32_t gpio_tick)
{
	int bitlen;

	bitlen = gpio_tick - bitstart;
	bitstart = gpio_tick;
	if (bitlen < 0) bitlen += 4294967295;

	if (running == STATE_START && gpio_level == 0) {
		running = STATE_DHT_RESPONDED;
		return;
	}
	if (running == STATE_DHT_RESPONDED && gpio_level == 1) {
		running = STATE_GET_READY;
		return;
	}
	if (running == STATE_GET_READY && gpio_level == 0) {
		running = STATE_BIT_LOW;
		return;
	}
	if (running == STATE_BIT_LOW && gpio_level == 1) {
		running = STATE_BIT;
		return;
	}
	if (running == STATE_BIT && gpio_level == 0) {
		running = STATE_BIT_LOW;

		byte[nbyte] <<= 1;
		if (bitlen > 49) byte[nbyte] |= 1;
		if (++nbit == 8) {
			nbit = 0;
			if (++nbyte == 5) {
				gpioSetAlertFunc(DHT22PIN, NULL);
				running = STATE_END;
			}
		}
		return;
	}
	// state machine is broken
	gpioSetAlertFunc(DHT22PIN, NULL);
	running = STATE_END;
}

int
main()
{
	if (gpioInitialise() < 0)
		exit(1);

	struct timespec twenty_us = { 0, 20000 };

	bzero(byte, sizeof byte);
	nbit = nbyte = 0;
	running = STATE_START;

	gpioSetPullUpDown(DHT22PIN, PI_PUD_OFF);

	// to initiate a request, send 18ms low then 20-40us high
	gpioSetMode(DHT22PIN, PI_OUTPUT);
	gpioWrite(DHT22PIN, 0);
	gpioSleep(PI_TIME_RELATIVE, 0, 18000);	// 18ms

	gpioWrite(DHT22PIN, 1);
	gpioDelay(20);	// 20us
	// DHT22 will respond by 80ms low then high

	bitstart = gpioTick();
	gpioSetMode(DHT22PIN, PI_INPUT);

	// register callback to read bits
	gpioSetAlertFunc(DHT22PIN, readbit);
	while (running) {
		gpioSleep(0, 0, 500);	// 500ms
	}
	int sum = (byte[0] + byte[1] + byte[2] + byte[3]) & 0xFF;
	if (sum != byte[4])
		exit(1);		// silent failure

	float h = (byte[0] * 256 + byte[1]) / 10.0f;
	printf("humidity %.1f%%\n", h);

	float t = ((byte[2] & 0x7F) * 256 + byte[3]) / 10.0f;
	if (byte[2] & 0x80)
		t *= -1.0f;
	printf("temperature %.1f°C\n", t);

	exit(0);
}

//end
