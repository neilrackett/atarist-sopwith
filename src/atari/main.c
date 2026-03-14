long _stksize = 65536L;

#include "swmain.h"

#include <osbind.h>
#include <stdint.h>

long disable_keyclick(void)
{
	volatile uint8_t *conterm = (volatile uint8_t *)0x0484;
	*conterm &= ~0x01;
	return 0;
}

int main(int argc, char *argv[])
{
	printf("Loading ST Sopwith...");
	Supexec(disable_keyclick);

	return swmain(argc, argv);
}
