// Networking is not implemented for the Atari ST port. If you'd like to add
// it, you're welcome to do so and send a pull request!

#include "std.h"
#include "tcpcomm.h"

void commconnect(char *host)
{
	(void) host;
}

void commlisten(void)
{
}

int commin(void)
{
	return -1;
}

void commout(unsigned char c)
{
	(void) c;
}

void commterm(void)
{
}

bool isNetworkGame(void)
{
	return false;
}
