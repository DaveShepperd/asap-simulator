/* MIT license. See LICENSE.md for details */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "vlda_structs.h"
#include "segdef.h"
#include "formats.h"
#include "get_stb.h"

#ifndef n_elts
	#define n_elts(x) (int)(sizeof(x)/sizeof((x)[0]))
#endif

HashEntry_t *getHashEntry(Asap_t *asap)
{
	HashEntry_t *ret=NULL;
	if ( !asap->hashesPool )
	{
		if ( !asap->numHashes )
			return ret;
		asap->hashesPool = (HashEntry_t *)malloc(asap->numHashes*sizeof(HashEntry_t));
		if ( !asap->hashesPool )
		{
			fprintf(stderr,"Failed to allocate %ld bytes for hash table\n", asap->numHashes*sizeof(HashEntry_t));
			exit(1);
		}
	}
	if ( asap->numUsedHashes < asap->numHashes )
	{
		ret = asap->hashesPool + asap->numUsedHashes;
		++asap->numUsedHashes;
	}
	return ret;
}

static void insertIntoTable(Asap_t *asap, uint32_t value, const char *name)
{
	HashEntry_t *hash = getHashEntry(asap);
	if ( hash )
	{
		hash->value = value;
		hash->name = name;
		hash->next = asap->hashTableTop[(value % HASH_TABLE_ENTRIES)];
		asap->hashTableTop[(value % HASH_TABLE_ENTRIES)] = hash;
	}
}

const HashEntry_t *findHash(Asap_t *asap, uint32_t value)
{
	const HashEntry_t *res;
	if ( !asap->numUsedHashes )
		return NULL;
	res = asap->hashTableTop[(value % HASH_TABLE_ENTRIES)];
	while ( res )
	{
		if ( res->value == value )
			break;
		res = res->next;
	}
	return res;
}

const HashEntry_t *findHashByName(Asap_t *asap, const char *name)
{
	int ii;
	for ( ii = 0; ii < HASH_TABLE_ENTRIES; ++ii )
	{
		HashEntry_t *he;
		he = asap->hashTableTop[ii];
		while ( he )
		{
			if ( !strcasecmp(he->name, name) )
				return he;
			he = he->next;
		}
	}
	return NULL;
}

typedef struct
{
	int num;
	const char *str;
	size_t structSize;
} Xref_t;

static const Xref_t ObjCodes[] =
{
	{ VLDA_ABS, "ABS", sizeof(VLDA_abs) },  /* vlda relocatible text record */
	{ VLDA_TXT, "TXT" },    /* vlda relocatible text record */
	{ VLDA_GSD, "GSD" },    /* psect/symbol definition record */
	{ VLDA_ORG,	"ORG" },    /* set the PC record */
	{ VLDA_ID,  "ID", sizeof(VLDA_id) },   /* misc header information */
	{ VLDA_EXPR, "EXPR" },  /* expression */
	{ VLDA_TPR,	"TPR" },    /* transparent record (raw data follows */
	{ VLDA_SLEN, "SLEN", sizeof(VLDA_slen) },    /* segment length */
	{ VLDA_XFER, "XFER", sizeof(VLDA_abs) }, /* transfer address */
	{ VLDA_TEST, "TEST", sizeof(VLDA_test) },   /* test and display message if result is false */
	{ VLDA_DBGDFILE, "DBGFILE", sizeof(VLDA_dbgdfile) }, /* dbg file specification */
	{ VLDA_DBGSEG, "DBGSEG", sizeof(VLDA_dbgseg) }, /* dbg segment descriptors */
	{ VLDA_BOFF, "BOFF", sizeof(VLDA_test) },   /* branch offset out of range test */
	{ VLDA_OOR,	"OOR", sizeof(VLDA_test) }      /* operand value out of range test */
};

static uint16_t getU16(const uint8_t *rcd)
{
	uint16_t ans;
	ans = (rcd[1] << 8) | rcd[0];
	return ans;
}

static uint32_t getU32(const uint8_t *rcd)
{
	uint32_t ans;
	ans = (rcd[3] << 24) | (rcd[2] << 16) | (rcd[1] << 8) | rcd[0];
	return ans;
}

static const char* getObjCode(int code)
{
	int ii;
	for ( ii = 0; ii < n_elts(ObjCodes); ++ii )
	{
		if ( ObjCodes[ii].num == code )
			return ObjCodes[ii].str;
	}
	return "*Undefined*";
}

int get_stb(Asap_t *asap)
{
	int fd, sts, recNum;
	uint8_t *rcdPtr;
	struct stat st;
	const char *codeName;
	uint16_t cnt;
	uint8_t typ, pad;
	
	sts = stat(asap->stbFilename, &st);
	if ( sts < 0 )
	{
		printf("Unable to stat '%s': %s\n", asap->stbFilename, strerror(errno));
		return 1;
	}
	asap->stbFileContents = (uint8_t *)malloc(st.st_size);
	if ( !asap->stbFileContents )
	{
		printf("Unable to allocate %ld bytes\n", st.st_size);
		return 1;
	}
	fd = open(asap->stbFilename, O_RDONLY);
	if ( fd < 0 )
	{
		printf("Unable to open for read '%s': %s\n", asap->stbFilename, strerror(errno));
		return 1;
	}
	sts = read(fd, asap->stbFileContents, st.st_size);
	if ( sts < 0 )
	{
		printf("Error reading '%s': %s\n", asap->stbFilename, strerror(errno));
		return 1;
	}
	if ( sts == 0 )
	{
		printf("Premature EOF on '%s': %s\n", asap->stbFilename, strerror(errno));
		return 1;
	}
	close(fd);
	/* count GSD records */
	rcdPtr = asap->stbFileContents;
	recNum = 0;
	while ( rcdPtr < asap->stbFileContents + st.st_size )
	{
		cnt = (rcdPtr[1] << 8) | rcdPtr[0];
		typ = rcdPtr[2];
		
		codeName = getObjCode(typ);
		pad = (cnt & 1);
		if ( cnt >= 16384 )
		{
			fprintf(stderr,"get_stb(): %3d: 0x%04X%c %02X(%s) Record count >= 16384. Probably out of sync\n", recNum, cnt, pad ? '*' : ' ', typ, codeName);
			break;
		}
		if ( typ == VLDA_GSD )
			++asap->numHashes;
		rcdPtr += cnt + 2 + pad;
		++recNum;
	}
	/* Insert GSD records */
	rcdPtr = asap->stbFileContents;
	recNum = 0;
	while ( rcdPtr < asap->stbFileContents + st.st_size )
	{
		cnt = (rcdPtr[1] << 8) | rcdPtr[0];
		typ = rcdPtr[2];

		pad = (cnt & 1);
		if ( typ == VLDA_GSD )
		{
			uint16_t flags = getU16(rcdPtr+2+1);
			int16_t  noff = getU16(rcdPtr+2+3);
			uint32_t value = getU32(rcdPtr+2+7);
			if ( (flags & VSYM_SYM) && noff)
			{
				const char *name = (const char *)(rcdPtr+2) + noff;
				int len = strlen(name);
				if ( len > asap->longestName )
					asap->longestName = len;
				insertIntoTable(asap, value, name );
			}
		}
		rcdPtr += cnt + 2 + pad;
	}
	if ( 0 && asap->verbose )
	{
		for ( fd = 0; fd < HASH_TABLE_ENTRIES; ++fd )
		{
			HashEntry_t *he;
			he = asap->hashTableTop[fd];
			if ( he )
			{
				printf("%08X: '%s'", he->value, he->name);
				he = he->next;
				while ( he )
				{
					printf(" '%s'", he->name);
					he = he->next;
				}
				printf("\n");
			}
		}
	}
	return 0;
}

