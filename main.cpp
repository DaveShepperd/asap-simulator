#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "asapExecute.h"
#include "get_stb.h"

static Asap_t asap;

static int help_em(const char *us)
{
	fprintf(stderr,"Usage: %s [-hiv] [-e ptr] path-to-image\n"
			"Where:\n"
			"-e ptr  - place in sim memory where errno is located. Defaults to 0x1BC\n"
			"-h      - this message\n"
			"-i      - set interactive mode\n"
			"-s      - set stack size (default 32768)"
			"-S path - point to .stb file to get symbols\n"
			"-v      - increase verbosity\n"
			,us);
	return 1;
}

int main(int argc, char *argv[])
{
	struct stat st;
	int opt, sts, fd, errnoPtrSet=0;
	uint32_t errnoPtr=0;
	const char *imageName;
	char *endp;
	
	while ( (opt = getopt(argc, argv, "e:his:S:v")) != -1 )
	{
		switch (opt)
		{
		case 'e':
			endp = NULL;
			errnoPtr = strtol(optarg,&endp,0);
			if ( !endp || *endp )
			{
				fprintf(stderr,"Invalid errno offset: '%s'\n", optarg);
				return 1;
			}
			errnoPtrSet = 1;
			break;
		case 'v':
			++asap.verbose;
			break;
		case 'i':
			asap.interactive = true;
			break;
		case 's':
			endp = NULL;
			asap.stackSize = strtol(optarg,&endp,0);
			if ( !endp || *endp || asap.stackSize <= 0 || asap.stackSize > 1024 )
			{
				fprintf(stderr,"Invalid stack size: '%s'\n", optarg);
				return 1;
			}
			break;
		case 'S':
			asap.stbFilename = optarg;
			break;
		case 'h':
		default: /* '?' */
			return help_em(argv[0]);
		}
	}
	if ( !asap.stackSize )
		asap.stackSize = 32768;
	if ( optind >= argc )
	{
		fprintf(stderr, "Expected path to image after options\n");
		return help_em(argv[0]);
	}
	imageName = argv[optind];
	sts = stat(imageName, &st);
	if ( sts < 0 )
	{
		printf("Unable to stat '%s': %s\n", imageName, strerror(errno));
		return 1;
	}
	asap.memLen = st.st_size;
	asap.mem = (uint8_t *)calloc(asap.memLen+asap.stackSize,1);
	if ( !asap.mem )
	{
		printf("Unable to allocate %d bytes\n", asap.memLen+asap.stackSize);
		return 1;
	}
	fd = open(imageName, O_RDONLY);
	if ( fd < 0 )
	{
		printf("Unable to open for read '%s': %s\n", imageName, strerror(errno));
		return 1;
	}
	sts = read(fd, asap.mem, st.st_size);
	if ( sts < 0 )
	{
		printf("Error reading '%s': %s\n", imageName, strerror(errno));
		return 1;
	}
	if ( sts == 0 )
	{
		printf("Premature EOF on '%s': %s\n", imageName, strerror(errno));
		return 1;
	}
	close(fd);
	if ( asap.stbFilename )
	{
		if ( get_stb(&asap) )
			return 1;
	}
	if ( !errnoPtrSet && !errnoPtr )
		errnoPtr = 0x1BC;
	if ( errnoPtr >= asap.memLen )
	{
		if ( errnoPtr >= asap.memLen + asap.stackSize )
			printf("WARNING: errno (%08X) is completely outside image 0x00000000-0x%08X. Not set.\n",
				   errnoPtr, asap.memLen + asap.stackSize -1);
		else
			printf("WARNING: errno (0x%08X) is outside image and into stack space 0x00000000-0x%08X. Not set.\n",
				   errnoPtr, asap.memLen-1);
	}
	else if ( errnoPtrSet && errnoPtr )
		asap.errnoPtr = (int *)(asap.mem + errnoPtr);
	simulateAsap(&asap);
	return 0;
}

