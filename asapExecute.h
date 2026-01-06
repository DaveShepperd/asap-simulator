#ifndef _ASAPEXECUTE_H_
#define _ASAPEXECUTE_H_

typedef struct
{
	uint32_t registers[32];
	uint32_t pc;
	uint32_t status;
	/* Status bits:
	   0 - carry
	   1 - overflow
	   2 - zero
	   3 - negative
	   4 - Interrupt enable
	   5 - Previous Interrupt enable
	*/
	uint32_t memLen;
	const char *msg;
	uint8_t mem[65536*4];
} Asap_t;

extern void asapExecute(Asap_t *asap);

#endif	/* _ASAPEXECUTE_H_ */
