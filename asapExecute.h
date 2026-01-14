#ifndef _ASAPEXECUTE_H_
#define _ASAPEXECUTE_H_

#define HASH_TABLE_ENTRIES (127)

typedef struct HashEntry_t
{
	struct HashEntry_t *next;
	uint32_t value;
	const char *name;
} HashEntry_t;

typedef struct
{
	uint32_t registers[32];
	uint32_t pc;
	uint32_t status;
	uint32_t memLen;
	uint32_t breakPoint;
	const char *msg;
	const char *stbFilename;
	uint8_t *stbFileContents;
	char errorMsg[128];
	char showText[128];
	char regName[3][8];
	int showTextLen;
	char stsTxt[16];
	int dstReg, src1Reg, src2;
	uint32_t result;
	int64_t bDst, bSrc1, bSrc2;
	uint8_t *mem;
	uint8_t stsMask;
	int stackSize;
	int verbose;
	int pcInc;
	int brTarget;
	int *errnoPtr;
	HashEntry_t *hashesPool;
	int numUsedHashes;
	int numHashes;
	HashEntry_t *hashTableTop[HASH_TABLE_ENTRIES];
	int longestName;
	bool affectStatus;
	bool interactive;
	bool cannotContinue;
	bool breakPointSet;
} Asap_t;

extern void simulateAsap(Asap_t *asap);
extern char *mkStsTxt(Asap_t *asap);

#endif	/* _ASAPEXECUTE_H_ */
