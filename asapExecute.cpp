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

/* Status bits:
   0 - carry
   1 - overflow
   2 - zero
   3 - negative
   4 - Interrupt enable
   5 - Previous Interrupt enable
*/

#define CARRY		(1<<0)
#define OVERFLOW	(1<<1)
#define ZERO		(1<<2)
#define NEGATIVE	(1<<3)

static int chkBranch(Asap_t *asap, int condition)
{
	bool N,Z,V,C;
	int status = asap->status;
	
	C = status&1;
	status >>= 1;
	V = status&1;
	status >>= 1;
	Z = status&1;
	status >>= 1;
	N = status&1;
	switch (condition)
	{
	case 0:		/* strictly positive (Z|N) == 0 */
		asap->msg = "BSP";
		return !(Z|N);
	case 1:		/* minus or zero (Z|N) == 1*/
		asap->msg = "BMZ";
		return (Z|N);
	case 2:		/* greater than ((N ^ V) | Z) == 0 */
		asap->msg = "BGT";
		return !((N^V)|Z);
	case 3:		/* less than or equal ((N ^ V) | Z) == 1 */
		asap->msg = "BLE";
		return ((N^V)|Z);
	case 4:		/* greater than or equal  (N ^ V) == 0 */
		asap->msg = "BGT";
		return !(N^V);
	case 5:		/* less than  (N ^ V) == 1 */
		asap->msg = "BLT";
		return (N^V);
	case 6:		/* high (~C | Z) == 0 */
		asap->msg = "BHI";
		return !(!C | Z);
	case 7:		/* low or equal (~C | Z) == 1 */
		asap->msg = "BLE";
		return (!C | Z);
	case 8:		/* carry clear C == 0*/
		asap->msg = "BCC";
		return !C;
	case 9:		/* carry set C == 1 */
		asap->msg = "BCS";
		return C;
	case 10:	/* Plus N == 0 */
		asap->msg = "BPL";
		return !N;
	case 11:	/* Minus N == 1 */
		asap->msg = "BMI";
		return N;
	case 12:	/* Not equal Z == 0 */
		asap->msg = "BNE";
		return !Z;
	case 13:	/* Equal Z == 1 */
		asap->msg = "BEQ";
		return Z;
	case 14:	/* Overflow V == 0 */
		asap->msg = "BVC";
		return !V;
	case 15:	/* Overflow V == 1 */
		asap->msg = "BVS";
		return V;
	default:
		break;
	}
	asap->msg = "Illegal branch";
	return 2;
}

void asapExecute(Asap_t *asap)
{
	int opcode;
	bool takeBranch;
	int brTarget, pcInc, brOffset, condition, dstReg, src1Reg, src2;
	uint32_t instruction;
	
	brTarget = 0;
	pcInc = 4;
	while ( 1 )
	{
		uint32_t *mem = (uint32_t *)(asap->mem+asap->pc);
		instruction = *mem;
		opcode = (instruction >> 27)&0x1F;
		printf("%08X: %08X - ", asap->pc, instruction);
		if ( brTarget )
		{
			asap->pc = brTarget;
			brTarget = 0;
			pcInc = 0;
		}
		switch (opcode)
		{
		default:
		case 0:
		case 0x1F:
			printf("Illegal opcode. Terminated.\n");
			return;
		case 1:
			/* branch? */
			condition = (instruction>>22)&0x1F;
			condition = chkBranch(asap,condition);
			if ( condition == 2 )
			{
				printf("%s. Terminated.\n", asap->msg);
				return;
			}
			brOffset = instruction & ((1 << 22) - 1);
			if ( (brOffset&(1<<21)) )
				brOffset |= 0xFFC00000;
			brOffset *= 4;
			printf("%s %+d (target=%08X)%s\n", asap->msg, brOffset, asap->pc+brOffset, condition ? " Taken":" Not taken");
			if ( condition )
			{
				if ( (asap->pc+brOffset < 0 || asap->pc+brOffset > (int)sizeof(asap->mem))  )
				{
					printf("Would have branched out of range. Terminated.\n");
					return;
				}
				brTarget = asap->pc+brOffset;
			}
			break;
		case 2:
			/* BSR and BRA */
			dstReg = (instruction>>22)&0x1F;
			brOffset = instruction & ((1 << 22) - 1);
			if ( (brOffset&(1<<21)) )
				brOffset |= 0xFFC00000;
			brOffset *= 4;
			brTarget = asap->pc+brOffset;
			if ( dstReg == 0 )
				printf("BRA %+d (target=%08X)\n", brOffset, brTarget);
			else
				printf("BSR %%%d,%+d (target=%08X)\n", dstReg, brOffset, brTarget);
			if ( (asap->pc+brOffset < 0 || asap->pc+brOffset > (int)sizeof(asap->mem))  )
			{
				printf("Would have branched out of range. Terminated.\n");
				return;
			}
			break;
		case 3:
			printf("LEA\n");
			break;
		case 4:
			printf("LEAS\n");
			break;
		case 5:
			printf("SUBR\n");
			break;
		case 6:
			printf("XOR\n");
			break;
		case 7:
			printf("XORN\n");
			break;
		case 8:
			printf("ADD\n");
			break;
		case 9:
			printf("SUB\n");
			break;
		case 0x0A:
			printf("ADDC\n");
			break;
		case 0x0B:
			printf("SUBC\n");
			break;
		case 0x0C:
			printf("AND\n");
			break;
		case 0x0D:
			printf("ANDN\n");
			break;
		case 0x0E:
			printf("OR\n");
			break;
		case 0x0F:
			printf("ORN\n");
			break;
		case 0x10:
			printf("LD\n");
			break;
		case 0x11:
			printf("LDS\n");
			break;
		case 0x12:
			printf("LDUS\n");
			break;
		case 0x13:
			printf("STS\n");
			break;
		case 0x14:
			printf("ST\n");
			break;
		case 0x15:
			printf("LDB\n");
			break;
		case 0x16:
			printf("LDUB\n");
			break;
		case 0x17:
			printf("STB\n");
			break;
		case 0x18:
			printf("ASHR\n");
			break;
		case 0x19:
			printf("LSHR\n");
			break;
		case 0x1A:
			printf("SHL\n");
			break;
		case 0x1B:
			printf("ROTL\n");
			break;
		case 0x1C:
			printf("GETPS\n");
			break;
		case 0x1D:
			printf("PUTPS\n");
			break;
		case 0x1E:
			printf("JSR\n");
			break;
		}
		asap->pc += pcInc;
		pcInc = 4;
	}
}

