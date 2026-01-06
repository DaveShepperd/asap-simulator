#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "asapExecute.h"

static Asap_t asap;

static void dumpMem(const Asap_t *asap)
{
	int ii;
	const uint32_t *ptr;
	
	ptr = asap->registers;
	printf("pc = %08X, status = %08X, memLen = %d.\n", asap->pc, asap->status, asap->memLen);
	for (ii=0; ii < 32; ++ii, ++ptr)
	{
		if ( !(ii&7) )
			printf("%sreg %2d:", ii ? "\n":"", ii);
		printf("  %08X", *ptr);
	}
	printf("\n\n");
	ptr = (uint32_t *)asap->mem;
	for (ii=0; ii < asap->memLen/4; ++ii, ++ptr)
	{
		if ( !(ii&7) )
			printf("%s%08X:", ii ? "\n":"", ii*4);
		printf("  %08X", *ptr);
	}
	printf("\n\n");
}

int main(int argc, char *argv[])
{
	struct stat st;
	int len,sts,fd;
	
	sts = stat(argv[1],&st);
	if ( sts < 0 )
	{
		printf("Unable to stat '%s': %s\n", argv[1], strerror(errno));
		return 1;
	}
	len = st.st_size;
	if ( len > (int)sizeof(asap.mem) )
	{
		printf("Input file of %d bytes is too big. Has to be less than %ld", len, sizeof(asap.mem));
		return 1;
	}
	fd = open(argv[1],O_RDONLY);
	if ( fd < 0 )
	{
		printf("Unable to open for read '%s': %s\n", argv[1], strerror(errno));
		return 1;
	}
	len = read(fd,asap.mem,st.st_size);
	if ( len < 0 )
	{
		printf("Error reading '%s': %s\n", argv[1], strerror(errno));
		return 1;
	}
	if ( len == 0 )
	{
		printf("Premature EOF on '%s': %s\n", argv[1], strerror(errno));
		return 1;
	}
	asap.memLen = len;
	close(fd);
	printf("Before execution (%d bytes):\n", len);
	dumpMem(&asap);
	asapExecute(&asap);
	printf("After execution (%d bytes):\n", len);
	dumpMem(&asap);
	return 0;
}

