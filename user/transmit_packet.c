#include <inc/lib.h>
#include <inc/stdio.h>
#include <inc/string.h>

void umain(int argc, char **argv)
{
	char *data = "Hello world from user space";
	sys_transmit_packet(data, strlen(data));
	printf("packet of size %d and content: %s has been transmitted\n",
	       strlen(data), data);
}
