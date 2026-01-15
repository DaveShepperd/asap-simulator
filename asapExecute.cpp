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
#include "lclreadline.h"
#include "qa.h"
#include "syscalls.h"
#include "get_stb.h"

#define CARRY		(1<<0)
#define OVERFLOW	(1<<1)
#define ZERO		(1<<2)
#define NEGATIVE	(1<<3)
#define IENABLE		(1<<4)
#define PIENABLE	(1<<5)
#define BSR_INC		(8)		/* Spec says this should be 4, but some real code assumes 8 */

static int chkBranch(Asap_t *asap)
{
	bool C,V,Z,N;
	int status = asap->status, condition=asap->dstReg;
	
	C = (status&CARRY) ? 1 : 0;
	V = (status&OVERFLOW) ? 1 : 0;
	Z = (status&ZERO) ? 1 : 0;
	N = (status&NEGATIVE) ? 1 : 0;
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
		asap->msg = "BLO(BCC)";
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

const char *mkRegName(Asap_t *asap, int num, int reg)
{
	if ( reg == 29 )
		return ".SP";
	if ( reg == 28 )
		return ".RP";
	if ( reg == 27 )
		return ".FP";
	if ( reg == 26 )
		return ".LP";
	snprintf(asap->regName[num],sizeof(asap->regName[num]),"%%%d", reg);
	return asap->regName[num];
}

char *mkStsTxt(Asap_t *asap, bool flag)
{
	if ( flag )
	{
		snprintf(asap->stsTxt, sizeof(asap->stsTxt), ", sts %c%c%c%c%c%c, ",
				(asap->status&PIENABLE)	? 'P': '-',
				(asap->status&IENABLE)	? 'I': '-',
				(asap->status&NEGATIVE)	? 'N': '-',
				(asap->status&OVERFLOW)	? 'V': '-',
				(asap->status&ZERO)		? 'Z': '-',
				(asap->status&CARRY)	? 'C': '-'
				 );
	}
	else
		asap->stsTxt[0] = 0;
	return asap->stsTxt;
}

/* Status bits:
   0 - carry
   1 - overflow
   2 - zero
   3 - negative
   4 - Interrupt enable
   5 - Previous Interrupt enable
*/

typedef struct
{
	uint32_t bit;
	uint32_t mask;
} BitMask_t;
static const BitMask_t BitMasks[] =
{
	{ 1<<7,  0x000000FF },	/* 0 */
	{ 1<<15, 0x0000FFFF },	/* 1 */
	{ (uint32_t)1<<31, (uint32_t)0xFFFFFFFF } /* 2 */
};

/* shiftCnt is 0, 1 or 2 for byte, short or long */
static void setStatus(Asap_t *asap, int shiftCnt)
{
	bool carry = (asap->bDst & 0x100000000);	// No matter what, bit 32 of result is carry
	static const uint32_t Bit31 = 1<<31;
	
	asap->status &= ~asap->stsMask;
	if ( (asap->stsMask & CARRY) && carry )
		asap->status |= CARRY;
	if ( (asap->stsMask & OVERFLOW) )
	{
				// If both sources were negative and the result was positve, it's an overflow
		if (   (((asap->bSrc2 & asap->bSrc1) & Bit31) && !carry)
			   // or if both sources were positive and the result was negative, it's an overflow
			|| (!((asap->bSrc2 | asap->bSrc1) & Bit31) && carry)
		   )
			asap->status |= OVERFLOW;
	}
	if ( (asap->stsMask & ZERO) && !(asap->bDst&BitMasks[shiftCnt].mask) )
		asap->status |= ZERO;
	if ( (asap->stsMask & NEGATIVE) && (asap->bDst&BitMasks[shiftCnt].bit) )
		asap->status |= NEGATIVE;
}

static void getALUargs(Asap_t *asap)
{
	asap->bDst = asap->registers[asap->dstReg];
	asap->bSrc1 = asap->registers[asap->src1Reg];
	if ( asap->src2 >= 0xFFE0 )
		asap->bSrc2 = asap->registers[asap->src2-0xFFE0];
	else
		asap->bSrc2 = asap->src2;
	return;
}

/* shiftCnt is 0, 1 or 2  */
static uint32_t getLSargs(Asap_t *asap, uint32_t instruction, int shiftCnt)
{
	uint32_t ans;
	int src1, src2, src2IsReg=0;
	
	src1 = (instruction>>16)&0x1F;
	src2 = (instruction&0xFFFF);
	ans = asap->registers[src1];
	if ( src2 >= 0xFFE0 )
	{
		src2 = asap->registers[src2-0xFFE0];
		src2IsReg = 1;
	}
	ans += src2*(1<<shiftCnt);
	if ( src1 == 29 || (src2IsReg && src2 == 29) )
	{
		if ( ans < asap->memLen || ans > asap->memLen + asap->stackSize )
		{
			snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of STACK range of memory %08X-%08X\n",
					 ans, asap->memLen, asap->memLen + asap->stackSize);
		}
	}
	return ans;
}

static void commonAluOut(Asap_t *asap, const char *opc, const char *oper )
{
	int src2 = asap->src2;
	if ( asap->affectStatus )
		setStatus(asap,2);
	asap->result = asap->bDst & 0xFFFFFFFF;
	if ( src2 >= 0xFFE0 )
	{
		src2 -= 0xFFE0;
		asap->showTextLen += snprintf(
			asap->showText+asap->showTextLen,
			sizeof(asap->showText)-asap->showTextLen,
			"%s%s %s,%s,%s ; dst gets %08X = %08lX %s %08X%s\n",
			   opc,
			   asap->affectStatus ? ".C":"", 
			   mkRegName(asap, 0, asap->dstReg),
			   mkRegName(asap, 1, asap->src1Reg),
			   mkRegName(asap, 2, src2),
			   asap->result,
			   asap->bSrc1&0xFFFFFFFF,
			   oper,
			   asap->registers[src2],
			   mkStsTxt(asap,asap->affectStatus)
			   );
	}
	else
	{
		asap->showTextLen += snprintf(
			asap->showText+asap->showTextLen,
			sizeof(asap->showText)-asap->showTextLen,
			"%s%s %s,%s,%d (%04X) ; dst gets %08X = %08lX %s %08X%s\n",
			   opc,
			   asap->affectStatus ? ".C":"", 
			   mkRegName(asap, 0, asap->dstReg),
			   mkRegName(asap, 1, asap->src1Reg),
			   src2,
			   src2&0xFFFF,
			   asap->result,
			   asap->bSrc1&0xFFFFFFFF,
			   oper,
			   src2,
			   mkStsTxt(asap,asap->affectStatus)
			   );
	}
	if ( asap->dstReg )
		asap->registers[asap->dstReg] = asap->result;
}

/* mult is 0, 1 or 2 for byte, short or long */
/* Finish for LEA and LEAS */
static int commonLDSOut(Asap_t *asap, int shiftCnt)
{
	//                           0  1  2 
	static const int Numbs[] = { 2, 4, 8 };
	uint32_t res = asap->result&BitMasks[shiftCnt].mask;
	const HashEntry_t *he = shiftCnt == 2 ? findHash(asap,res) : NULL;
	
	if ( asap->affectStatus )
		setStatus(asap,shiftCnt);
	asap->showTextLen += snprintf(
		asap->showText+asap->showTextLen,
		sizeof(asap->showText)-asap->showTextLen,
		"; dst gets %0*X = %08X+%08X%s  %s\n",
		   Numbs[shiftCnt],
		   res,
		   asap->registers[asap->src1Reg],
		   (asap->src2 >= 0xFFE0 ? asap->registers[asap->src2-0xFFE0]:asap->src2)*(1<<shiftCnt),
		   mkStsTxt(asap,asap->affectStatus),
           he ? he->name : ""
		   );
	if ( asap->dstReg )
		asap->registers[asap->dstReg] = asap->result;
	if ( asap->errorMsg[0] )
		return 1;
	return 0;
}

/* shiftCnt is 0, 1 or 2 for byte, short or long */
/* Finish for LD, LDS and LDB */
static int commonLDIndOut(Asap_t *asap, uint32_t memIdx, int shiftCnt)
{
	//                           0  1  2 
	static const int Numbs[] = { 2, 4, 8 };
	uint32_t res = asap->result&BitMasks[shiftCnt].mask;
	const HashEntry_t *he = shiftCnt == 2 ? findHash(asap,memIdx) : NULL;

	if ( asap->affectStatus )
		setStatus(asap,shiftCnt);
	asap->showTextLen += snprintf(
		asap->showText+asap->showTextLen,
		sizeof(asap->showText)-asap->showTextLen,
		"; dst gets %0*X @%08X=%08X+%08X%s  %s\n",
		   Numbs[shiftCnt],
		   res,
		   memIdx,
		   asap->registers[asap->src1Reg],
		   (asap->src2 >= 0xFFE0 ? asap->registers[asap->src2-0xFFE0] : asap->src2)*(1<<shiftCnt),
		   mkStsTxt(asap,asap->affectStatus),
		   he ? he->name : ""
		   );
	if ( asap->dstReg )
		asap->registers[asap->dstReg] = asap->result;
	if ( asap->errorMsg[0] )
		return 1;
	return 0;
}

static int commonSTIndOut(Asap_t *asap, uint32_t memIdx, int shiftCnt)
{
	//                           0  1  2
	static const int Numbs[] = { 2, 4, 8 };
	uint32_t res = asap->result&BitMasks[shiftCnt].mask;
	const HashEntry_t *he = shiftCnt == 2 ? findHash(asap,memIdx) : NULL;

	if ( asap->affectStatus )
		setStatus(asap,shiftCnt);
	asap->showTextLen += snprintf(
		asap->showText+asap->showTextLen,
		sizeof(asap->showText)-asap->showTextLen,
		"; %0*X -> @%08X=%08X+%08X%s  %s\n",
		   Numbs[shiftCnt],
		   res,
		   memIdx,
		   asap->registers[asap->src1Reg],
		   (asap->src2 >= 0xFFE0 ? asap->registers[asap->src2-0xFFE0] : asap->src2)*(1<<shiftCnt),
		   mkStsTxt(asap,asap->affectStatus),
		   he ? he->name : ""
		   );
	if ( asap->errorMsg[0] )
		return 1;
	return 0;
}

static void mkInstText(Asap_t *asap, uint32_t instruction, const char *opc, int mult)
{
	if ( (instruction&0xFFFF) >= 0xFFE0 )
	{
		asap->showTextLen += snprintf(
			asap->showText+asap->showTextLen,
			sizeof(asap->showText)-asap->showTextLen,
			"%s%s %s, %s[%s]",
			opc,
			asap->affectStatus ? ".C":"",
			mkRegName(asap,0,asap->dstReg),
			mkRegName(asap,1, asap->src1Reg),
			mkRegName(asap,2,(instruction&0xFFFF)-0xFFE0)
		    );
	}
	else
	{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"%s%s %s, %s[%d (%04X)]",
				opc,
				   asap->affectStatus ? ".C":"",
				   mkRegName(asap,0,asap->dstReg),
				   mkRegName(asap,1,asap->src1Reg),
				   (instruction & 0xFFFF) * mult,
				   (instruction & 0xFFFF)
				);
	}
}

#if 0
static uint32_t getFileNo(const char *title, Asap_t *asap, int reg)
{
	uint32_t ptr;
	ptr = asap->registers[reg];
	if ( ptr > asap->memLen + asap->stackSize )
	{
		snprintf(asap->errorMsg, sizeof(asap->errorMsg),
				 "%s pointer of 0x%08X is out of range of 0x00000000-0x%08X",
				 title, ptr, asap->memLen + asap->stackSize-1 );
		return 0;
	}
	return *(uint32_t *)(asap->mem+ptr);
}
#endif

char *getStrPtr(const char *title, Asap_t *asap, int reg)
{
	uint32_t ptr;
	ptr = asap->registers[reg];
	if ( ptr > asap->memLen + asap->stackSize )
	{
		snprintf(asap->errorMsg, sizeof(asap->errorMsg), "%s pointer of 0x%08X is out of range of 0x00000000-0x%08X",
				 title, ptr, asap->memLen + asap->stackSize-1 );
		return 0;
	}
	return (char *)(asap->mem+ptr);
}

static int doSyscall(Asap_t *asap, int call)
{
	FILE *fp;
	int len, fno, sts;
	char *strPtr;
	
	if ( asap->verbose )
		printf("%s\n", asap->showText);
	if ( asap->numHashes )
	{
		asap->showTextLen = snprintf(
					asap->showText,
					sizeof(asap->showText),
					"%-*.*s                      ",
					asap->longestName,
					asap->longestName,
					" ");
	}
	else
	{
		strncpy( asap->showText,
				 "                     ",
				 sizeof(asap->showText));
		asap->showTextLen = strlen(asap->showText);
	}
	asap->showTextLen += snprintf(
		asap->showText+asap->showTextLen,
		sizeof(asap->showText)-asap->showTextLen,
		"SYSCALL %d; %s\n", call, mkStsTxt(asap,true));
	switch (call)
	{
	default:
	case SYSCALL_EXIT:
		asap->showTextLen += snprintf(
			asap->showText+asap->showTextLen,
			sizeof(asap->showText)-asap->showTextLen,
			"SYSCALL_EXIT(%d): Terminated.\n", asap->registers[1]);
		break;
	case SYSCALL_FFLUSH:
		fno = asap->registers[1];
		if ( asap->verbose )
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"fflush(%d).\n", fno);
		}
		else
		{
			fp = NULL;
			if ( fno == 1 )
				fp = stdout;
			else if ( fno == 2 )
				fp = stderr;
			if ( fp )
			{
				sts = fflush(fp);
				asap->registers[1] = sts;
				if ( asap->errnoPtr )
					*asap->errnoPtr = errno;
			}
			else
			{
				asap->registers[1] = -1;
				if ( asap->errnoPtr )
					*asap->errnoPtr = ENXIO;
			}
		}
		asap->registers[1] = 0;
		return 0;
	case SYSCALL_FPUTS:
		strPtr = getStrPtr("String",asap,1);
		fno = asap->registers[2];
		if ( asap->verbose )
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"fputs('%s',%d).\n", strPtr, fno);
		}
		else
		{
			fp = NULL;
			if ( fno == 1 )
				fp = stdout;
			else if ( fno == 2 )
				fp = stderr;
			if ( fp )
			{
				sts = fputs(strPtr, fp);
				asap->registers[1] = sts;
				if ( asap->errnoPtr )
					*asap->errnoPtr = errno;
			}
			else
			{
				asap->registers[1] = -1;
				if ( asap->errnoPtr )
					*asap->errnoPtr = ENXIO;
			}
		}
		return 0;
	case SYSCALL_FGETS:
		strPtr = getStrPtr("String",asap,1);
		len = asap->registers[2];
		fno = asap->registers[3];
		if ( asap->verbose )
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"fgets(%p,%d,%d). %s\n",
				strPtr,
				len,
				fno,
				fno ? "Not stdin. Ignored.":"Enter line of text:"
				);
			fputs(asap->showText,stdout);
			asap->showText[0] = 0;
			asap->showTextLen = 0;
		}
		if ( fno == 0 )
		{
			fflush(stdout);
			if ( !fgets(strPtr,len,stdin) )
			{
				asap->registers[1] = 0;
				if ( asap->errnoPtr )
					*asap->errnoPtr = EOF;
			}
		}
		else
		{
			asap->registers[1] = 0;
			if ( asap->errnoPtr )
				*asap->errnoPtr = ENXIO;
		}
		return 0;
	case SYSCALL_FPUTC:
		len = asap->registers[1];
		fno = asap->registers[2];
		if ( asap->verbose )
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"fputc(%02X(%c),%d)\n",
				len,
				isprint(len)?len:'.',
				fno
				);
		}
		else
		{
			fp = NULL;
			if ( fno == 1 )
				fp = stdout;
			else if ( fno == 2 )
				fp = stderr;
			if ( fp )
			{
				sts = fputc(len,fp);
				asap->registers[1] = sts;
				if ( asap->errnoPtr )
					*asap->errnoPtr = errno;
			}
			else
			{
				asap->registers[1] = -1;
				if ( asap->errnoPtr )
					*asap->errnoPtr = ENXIO;
			}
		}
		return 0;
	case SYSCALL_FGETC:
		fno = asap->registers[1];
		if ( asap->verbose )
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"fgetc(%d). %s\n",
				fno,
				fno ? "Not stdin. Ignored.":"Enter a character:"
				);
		    fputs(asap->showText,stdout);
			asap->showText[0] = 0;
			asap->showTextLen = 0;
		}
		if ( fno == 0 )
		{
			sts = fgetc(stdin);
			asap->registers[1] = sts;
			if ( asap->errnoPtr )
				*asap->errnoPtr = errno;
		}
		else
		{
			asap->registers[1] = -1;
			if ( asap->errnoPtr )
				*asap->errnoPtr = ENXIO;
		}
		return 0;
	}
	return 1;
}

static int executeInstruction(Asap_t *asap)
{
	int opcode, reg;
	int brOffset, condition;
	uint16_t src2;
	uint32_t instruction, memIdx;
	uint32_t *mem; 
	const HashEntry_t *he;
	
	mem = (uint32_t *)(asap->mem+asap->pcQue[0]);
	instruction = *mem;
	opcode = (instruction >> 27)&0x1F;
	asap->dstReg = (instruction>>22)&0x1F;
	asap->src1Reg = (instruction>>16)&0x1F;
	asap->src2 = (instruction&0xFFFF);
	asap->bDst = 0;
	asap->result = 0;
	asap->affectStatus = (instruction&(1<<21)) ? true : false;
	asap->errorMsg[0] = 0;
	if ( asap->numHashes )
	{
		char header[36];
		header[0] = 0;
		he = findHash(asap,asap->pcQue[0]);
		if ( he )
			snprintf(header,sizeof(header),"%s:",he->name);
		asap->showTextLen = snprintf(
					asap->showText,
					sizeof(asap->showText),
					"%-*.*s %08X: %08X - ",
					asap->longestName,
					asap->longestName,
					header,
					asap->pcQue[0],
					instruction);
	}
	else
	{
		asap->showTextLen = snprintf(
					asap->showText,
					sizeof(asap->showText),
					"%08X: %08X - ",
					asap->pcQue[0],
					instruction);
	}
	switch (opcode)
	{
	default:
	case 0:
	case 0x1F:
		asap->registers[30] = asap->pcQue[0];
		asap->registers[31] = asap->pcQue[1];
		asap->status = ((asap->status&IENABLE)<<1) | (asap->status&0xF);
		asap->showTextLen += snprintf(
			asap->showText+asap->showTextLen,
			sizeof(asap->showText)-asap->showTextLen,
			"Illegal opcode. r30 <- %08X, r31 <- %08X, %s"
			,asap->registers[30]
			,asap->registers[31]
			,mkStsTxt(asap,true)
			);
		src2 = (instruction&0xFFFF);
		reg = 0;
		if ( (src2 >= 0xFFE0) )
		{
			reg = src2 - 0xFFE0;
			src2 = 0;
		}
		else if ( src2 < 1 || src2 > 255 )
			src2 = 0;
		if (    !opcode
			 || (instruction > 0xF800FFE5)
			 || (!reg && !src2)
			 || reg > 5
		   )
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"Terminated.\n");
			return 1;
		}
		if ( reg )
			reg = asap->registers[reg];
		else
			reg = src2;
		if ( doSyscall(asap, reg) )
			return 1;
		break;
	case 1:
		/* branch? */
		condition = chkBranch(asap);
		if ( condition == 2 )
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"%s. Terminated.", asap->msg);
			return 1;
		}
		brOffset = instruction & ((1 << 22) - 1);
		if ( (brOffset&(1<<21)) )
			brOffset |= 0xFFC00000;
		brOffset *= 4;
		asap->affectStatus = 1;	/* allow it to show status */
		asap->showTextLen += snprintf(
			asap->showText+asap->showTextLen,
			sizeof(asap->showText)-asap->showTextLen,
			"%s %+d (%06X) ;%s branch to %08X, %s\n",
			   asap->msg,
			   brOffset,
			   instruction&0x3FFFFF,
			   mkStsTxt(asap,asap->affectStatus),
			   asap->pcQue[0]+brOffset,
			   condition ? " Taken":" Not taken");
		if ( condition )
		{
			if ( (asap->pcQue[0] + brOffset < 0 || asap->pcQue[0] + brOffset > asap->memLen) )
			{
				printf("%s\nWould have branched to %08X which is out of memory range %08X. Terminated.\n",
					   asap->showText,
					   asap->pcQue[0] + brOffset, asap->memLen);
				asap->showText[0] = 0;
				asap->showTextLen = 0;
				return 1;
			}
			asap->pcQue[2] = asap->pcQue[0]+brOffset;
		}
		break;
	case 2:
		/* BSR and BRA */
		brOffset = instruction & ((1 << 22) - 1);
		if ( (brOffset&(1<<21)) )
			brOffset |= 0xFFC00000;
		brOffset *= 4;
		asap->pcQue[2] = asap->pcQue[0]+brOffset;
		he = findHash(asap,asap->pcQue[2]);
		if ( asap->dstReg == 0 )
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"BRA %+d (%06X) (branch to %08X)  %s\n",
				brOffset,
				instruction&0x3FFFFF,
				asap->pcQue[2],
				he ? he->name:"");
		}
		else
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"BSR %s,%+d (%06X) ; dst gets %08X, branch to %08X  %s\n",
				   mkRegName(asap,0,asap->dstReg),
				   brOffset,
				   instruction&0x3FFFFF,
				   asap->pcQue[0] + BSR_INC,
				   asap->pcQue[2],
				   he ? he->name : "");
			asap->registers[asap->dstReg] = asap->pcQue[0]+BSR_INC;
		}
		if ( (asap->pcQue[0]+brOffset < 0 || asap->pcQue[0]+brOffset > asap->memLen)  )
		{
			printf("%s\nWould have branched to %08X which is out of memory range %08X. Terminated.\n",
				   asap->showText,
				   asap->pcQue[0] + brOffset, asap->memLen);
			asap->showText[0] = 0;
			asap->showTextLen = 0;
			return 1;
		}
		break;
	case 3:
		asap->stsMask = NEGATIVE|ZERO;
		asap->result = getLSargs(asap,instruction,2);
		asap->bDst = asap->result;
		mkInstText(asap,instruction,"LEA",4);
		if ( commonLDSOut(asap,2) )
			return 1;
		break;
	case 4:
		asap->stsMask = NEGATIVE|ZERO;
		asap->result = getLSargs(asap,instruction,1);
		asap->bDst = asap->result;
		mkInstText(asap,instruction,"LEAS",2);
		if ( commonLDSOut(asap,1) )
			return 1;
		break;
	case 5:
		asap->stsMask = CARRY|OVERFLOW|NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bSrc1 = ((~asap->bSrc1)&0xFFFFFFFF) + 1;	/* 2's compliment lower 32 bits */
		asap->bDst = asap->bSrc2+asap->bSrc1; /* so overflow and carry bit set properly */
		if ( asap->affectStatus )
			setStatus(asap,2);
		asap->result = asap->bDst & 0xFFFFFFFF;
		if ( asap->src2 >= 0xFFE0 )
		{
			asap->src2 -= 0xFFE0;
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"SUBR%s %s,%s,%s ; dst gets %08X <- %08X-%08X%s\n",
				   asap->affectStatus ? ".C":"",
				   mkRegName(asap,0,asap->dstReg),
				   mkRegName(asap,1,asap->src1Reg),
				   mkRegName(asap,2,asap->src2),
				   asap->result,
				   asap->registers[asap->src2],
				   asap->registers[asap->src1Reg],
				   mkStsTxt(asap,asap->affectStatus)
				   );
		}
		else
		{
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
				"SUBR%s %s,%s,%d (%X) ; dst gets %08X <- %08X-%08X%s\n",
				   asap->affectStatus ? ".C":"", 
				   mkRegName(asap,0,asap->dstReg),
				   mkRegName(asap,1,asap->src1Reg),
				   asap->src2,
				   asap->src2,
				   asap->result,
				   asap->src2,
				   asap->registers[asap->src1Reg],
				   mkStsTxt(asap,asap->affectStatus)
				   );
		}
		if (asap->dstReg )
			asap->registers[asap->dstReg] = asap->result;
		break;
	case 6:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1^asap->bSrc2;
		commonAluOut(asap,"XOR","^");
		break;
	case 7:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1^~asap->bSrc2;
		commonAluOut(asap,"XORN","^~");
		break;
	case 8:
		asap->stsMask = CARRY|OVERFLOW|NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1+asap->bSrc2;
		commonAluOut(asap,"ADD","+");
		break;
	case 9:
		asap->stsMask = CARRY|OVERFLOW|NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bSrc2 = ((~asap->bSrc2)&0xFFFFFFFF)+1; /* 2's compliment lower 32 bits so overflow and carry work */
		asap->bDst = asap->bSrc1+asap->bSrc2;
		commonAluOut(asap,"SUB","-");
		break;
	case 0x0A:
		asap->stsMask = CARRY|OVERFLOW|NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1+asap->bSrc2+(asap->status&CARRY);
		commonAluOut(asap,"ADDC","+");
		break;
	case 0x0B:
		asap->stsMask = CARRY|OVERFLOW|NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bSrc2 = ((~asap->bSrc2)&0xFFFFFFFF)+1+(asap->status&CARRY); /* so overflow check works */
		asap->bDst = asap->bSrc1+asap->bSrc2;
		commonAluOut(asap,"SUBC","-");
		break;
	case 0x0C:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1&asap->bSrc2;
		commonAluOut(asap,"AND","&");
		break;
	case 0x0D:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1&~asap->bSrc2;
		commonAluOut(asap,"ANDN","&~");
		break;
	case 0x0E:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1|asap->bSrc2;
		commonAluOut(asap,"OR","|");
		break;
	case 0x0F:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1|asap->bSrc2;
		commonAluOut(asap,"ORN","|~");
		break;
	case 0x10:
		asap->stsMask = NEGATIVE|ZERO;
		memIdx = getLSargs(asap,instruction,2);
		mkInstText(asap,instruction,"LD",4);
		if ( !asap->errorMsg[0] )
		{
			if ( memIdx > asap->memLen + asap->stackSize )
				snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of range of memory %08X\n", memIdx, asap->memLen + asap->stackSize);
			else
				asap->bDst = *(uint32_t *)(asap->mem + memIdx);
		}
		else
			asap->bDst = 0;
		asap->result = asap->bDst;
		if ( commonLDIndOut(asap,memIdx,2) )
			return 1;
		break;
	case 0x11:
		asap->stsMask = NEGATIVE|ZERO;
		memIdx = getLSargs(asap,instruction,1);
		mkInstText(asap,instruction,"LDS",2);
		if ( !asap->errorMsg[0] )
		{
			if ( memIdx > asap->memLen + asap->stackSize )
				snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of range of memory %08X\n", memIdx, asap->memLen + asap->stackSize);
			else
				asap->bDst = *(uint16_t *)(asap->mem + memIdx);
		}
		if ( (asap->bDst&0x8000) )
			asap->bDst |= 0xFFFF0000;
		asap->result = asap->bDst;
		if (commonLDIndOut(asap,memIdx,1))
			return 1;
		break;
	case 0x12:
		asap->stsMask = NEGATIVE|ZERO;
		memIdx = getLSargs(asap,instruction,1);
		mkInstText(asap,instruction,"LDUS",2);
		if ( !asap->errorMsg[0] )
		{
			if ( memIdx > asap->memLen + asap->stackSize )
				snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of range of memory %08X\n", memIdx, asap->memLen + asap->stackSize);
			else
				asap->bDst = *(uint16_t *)(asap->mem + memIdx);
		}
		asap->result = asap->bDst;
		if ( commonLDIndOut(asap,memIdx,1) )
			return 1;
		break;
	case 0x13:
		asap->stsMask = NEGATIVE|ZERO;
		memIdx = getLSargs(asap,instruction,1);
		mkInstText(asap,instruction,"STS",2);
		asap->result = asap->registers[asap->dstReg];
		asap->bDst = asap->result;
		if ( !asap->errorMsg[0] )
		{
			if ( memIdx > asap->memLen + asap->stackSize )
				snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of range of memory %08X\n", memIdx, asap->memLen + asap->stackSize);
			else
				*(uint16_t *)(asap->mem + memIdx) = asap->result;
		}
		if ( commonSTIndOut(asap,memIdx,1) )
			return 1;
		break;
	case 0x14:
		asap->stsMask = NEGATIVE|ZERO;
		memIdx = getLSargs(asap,instruction,2);
		mkInstText(asap,instruction,"ST",4);
		asap->result = asap->registers[asap->dstReg];
		asap->bDst = asap->result;
		if ( !asap->errorMsg[0] )
		{
			if ( memIdx > asap->memLen + asap->stackSize )
				snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of range of memory %08X\n", memIdx, asap->memLen + asap->stackSize);
			else
				*(uint32_t *)(asap->mem + memIdx) = asap->result;
		}
		if (commonSTIndOut(asap,memIdx,2))
			return 1;
		break;
	case 0x15:
		asap->stsMask = NEGATIVE|ZERO;
		memIdx = getLSargs(asap,instruction,0);
		mkInstText(asap,instruction,"LDB",1);
		if ( !asap->errorMsg[0] )
		{
			if ( memIdx > asap->memLen + asap->stackSize )
				snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of range of memory %08X\n", memIdx, asap->memLen + asap->stackSize);
			else
				asap->bDst = (uint8_t)asap->mem[memIdx];
		}
		if ( (asap->bDst&0x80) )
			asap->bDst |= 0xFFFFFF00;
		asap->result = asap->bDst;
		if (commonLDIndOut(asap,memIdx,0))
			return 1;
		break;
	case 0x16:
		asap->stsMask = NEGATIVE|ZERO;
		memIdx = getLSargs(asap,instruction,0);
		mkInstText(asap,instruction,"LDUB",1);
		if ( !asap->errorMsg[0] )
		{
			if ( memIdx > asap->memLen + asap->stackSize )
				snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of range of memory %08X\n", memIdx, asap->memLen + asap->stackSize);
			else
				asap->bDst = (uint8_t)asap->mem[memIdx];
		}
		asap->result = asap->bDst;
		if (commonLDIndOut(asap,memIdx,0))
			return 1;
		break;
	case 0x17:
		asap->stsMask = NEGATIVE|ZERO;
		memIdx = getLSargs(asap,instruction,0);
		mkInstText(asap,instruction,"STB",1);
		asap->bDst = asap->registers[asap->dstReg]&0xFF;
		asap->result = asap->bDst;
		if ( !asap->errorMsg[0] )
		{
			if ( memIdx > asap->memLen + asap->stackSize )
				snprintf(asap->errorMsg, sizeof(asap->errorMsg) - 1, "getLSargs(): memIdx %08X out of range of memory %08X\n", memIdx, asap->memLen + asap->stackSize);
			else
				asap->mem[memIdx] = asap->result;
		}
		if (commonSTIndOut(asap,memIdx,0))
			return 1;
		break;
	case 0x18:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1 >> asap->bSrc2;
		commonAluOut(asap,"ASHR",">>");
		break;
	case 0x19:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1 >> asap->bSrc2;
		commonAluOut(asap,"LSHR",">>");
		break;
	case 0x1A:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1 << asap->bSrc2;
		commonAluOut(asap,"SHL","<<");
		break;
	case 0x1B:
		asap->stsMask = NEGATIVE|ZERO;
		getALUargs(asap);
		asap->bDst = asap->bSrc1 << asap->bSrc2;
		asap->bDst |= asap->bDst>>32;
		commonAluOut(asap,"ROTL","<<");
		break;
	case 0x1C:
		asap->showTextLen += snprintf(
			asap->showText+asap->showTextLen,
			sizeof(asap->showText)-asap->showTextLen,
			"GETPS %s  ; dst gets %02X%s\n",
			mkRegName(asap,0,asap->dstReg),
			asap->status&0x3F,
			mkStsTxt(asap,true));
		if ( asap->dstReg )
			asap->registers[asap->dstReg] = asap->status;
		break;
	case 0x1D:
		if ( asap->src2 >= 0xFFE0 )
		{
			asap->status = asap->registers[asap->src2-0xFFE0]&0x3F;
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
			    "PUTPS %s  ; PS gets %02X%s",
				mkRegName(asap,0,asap->dstReg),
				asap->status,
				mkStsTxt(asap,true));
		}
		else
		{
			asap->status = asap->src2&0x3F;
			asap->showTextLen += snprintf(
				asap->showText+asap->showTextLen,
				sizeof(asap->showText)-asap->showTextLen,
			    "PUTPS %d    ; PS gets %02X%s\n",
				asap->src2,
				asap->src2&0x3F,
				mkStsTxt(asap,true));
		}
		break;
	case 0x1E:
		memIdx = getLSargs(asap,instruction,2);
		mkInstText(asap,instruction,"JSR",4);
		if ( asap->dstReg )
			asap->registers[asap->dstReg] = asap->pcQue[0]+BSR_INC;
		asap->pcQue[2] = memIdx;
		if ( asap->affectStatus )
			asap->status = ((asap->status&PIENABLE)>>1) | (asap->status&0x2F);
		he = findHash(asap,memIdx);
		asap->showTextLen += snprintf(
			asap->showText+asap->showTextLen,
			sizeof(asap->showText)-asap->showTextLen,
			"; dst gets %08X, jump to %08X%s  %s\n",
			   asap->pcQue[0]+BSR_INC,
			   memIdx,
			   mkStsTxt(asap,asap->affectStatus),
			   he ? he->name : ""
			   );
		if ( asap->errorMsg[0] )
			return 1;
		break;
	}
	asap->pcQue[0] = asap->pcQue[1];
	asap->pcQue[1] = asap->pcQue[2];
	asap->pcQue[2] = asap->pcQue[1]+4;
	return 0;
}

static void dumpRegs(Asap_t *asap)
{
	uint32_t ii;
	const uint32_t *ptr;

	ptr = asap->registers;
	printf("pcs=%08X,%08X,%08X %smemLen=%d, stackSize=%d.\n",
		   asap->pcQue[0],
		   asap->pcQue[1],
		   asap->pcQue[2],
		   mkStsTxt(asap,true),
		   asap->memLen,
		   asap->stackSize);
	for ( ii = 0; ii < 32; ++ii, ++ptr )
	{
		if ( !(ii & 7) )
			printf("%sreg %2d:", ii ? "\n" : "", ii);
		printf("  %08X", *ptr);
	}
	printf("\n");
}

static char *dmpAscii(char *dst, int columns, const uint8_t *rcd, int bytes)
{
	int ii;
	*dst++ = ' ';
	*dst++ = ' ';
	*dst++ = '|';
	for (ii=0; ii < bytes; ++ii)
	{
		*dst++ = isprint(*rcd) ? *rcd : '.';
		++rcd;
	}
	if ( ii < columns )
	{
		int jj;
		for (jj=ii; jj < columns; ++jj)
		{
			*dst++ = ' ';
		}
	}
	*dst++ = '|';
	*dst = 0;
	return dst;
}

static int hexDump(Asap_t *asap, uint32_t addr, uint32_t cnt)
{
	int len=0, items, banner;
	char *dst;
	char line[256];
	uint8_t *rcd, *linePtr;
	uint32_t ii, *rcd32;
	static const int Bytes=32;
	static const int MaxItems=8;
	
	items = 0;
	banner = 1;
	addr = addr & -4;
	cnt = cnt & -4;
	if ( cnt > asap->memLen )
		cnt = asap->memLen;
	if ( cnt > addr + asap->memLen )
		cnt = asap->memLen-addr;
	rcd = asap->mem + addr;
	linePtr = rcd;
	dst = line;
	for ( ii = 0; ii < cnt; )
	{
		if ( items >= MaxItems )
		{
			dmpAscii(dst, Bytes, linePtr, Bytes );
			banner = 1;
			dst = line;
		}
		if ( banner )
		{
			if ( ii )
			{
				printf("%s\n", line);
				memset(line,' ',sizeof(line)-1);
			}
			len = snprintf(line,sizeof(line),"%08X: ", addr);
			dst = line+len;
			addr += Bytes;
			banner = 0;
			items = 0;
			linePtr = rcd;
		}
		if ( items && !(items&3) )
			*dst++ = ' ';
		rcd32 = (uint32_t *)(rcd);
		dst += snprintf(dst, 10, " %08X", *rcd32);
		++items;
		rcd += 4;
		ii += 4;
	}
	if ( items && items <= MaxItems)
	{
		if ( cnt >= Bytes )
		{
			int jj;
			for (jj=items; jj < MaxItems; ++jj)
			{
				if ( jj && !(jj&3) )
					*dst++ = ' ';
				memset(dst,' ',10);
				dst += 9;
			}
		}
		dmpAscii(dst, Bytes, linePtr, items*4);
	}
	printf("%s\n", line);
	return 0;
}

typedef enum
{
	Nothing,
	Step,
	Memory,
	Registers,
	Verbose,
	Help,
	Continue,
	Run,
	Quit,
	Breakpoint
} Cmds_t;

void simulateAsap(Asap_t *asap)
{
//	asap->brTarget = 0;
//	asap->pcInc = 4;
	Cmds_t lastCmd=Nothing;
	uint32_t memFrom=0;
	int memLen=0, args;
	
	asap->pcQue[0] = 0;
	asap->pcQue[1] = 4;
	asap->pcQue[2] = 8;
	if ( !asap->interactive && asap->verbose )
	{
		printf("Before execution:\n");
		dumpRegs(asap);
		printf("\n");
	}
	while ( 1 )
	{
		if ( asap->interactive )
		{
			char ttybuf[128], token[sizeof(ttybuf)+1];
			char *ttp;
			ttp = ttybuf;
			if ( qa5(stdin, stdout, "asap-sim> ", sizeof(ttybuf), ttybuf) )
			{
				switch (_qaval_)
				{
				case EOF:
					printf("\n");
					return;
	
				case EOF - 1:
					fprintf(stderr, "\nI/O error; command ignored\n\n");
					purge_qa2(stdin, stdout);
					lastCmd = Nothing;
					memFrom = 0;
					memLen = 0;
					continue;

				case 1:
					break;
					
				default:
					fprintf(stderr, "\nUnhandled qa5() return: %d; command ignored\n\n", _qaval_);
					purge_qa2(stdin, stdout);
					lastCmd = Nothing;
					memFrom = 0;
					memLen = 0;
					continue;
				};
			}
	
			while ( isspace(*ttp) )
				++ttp;
			if ( !*ttp || *ttp == '!' || *ttp == ';' || *ttp == '#' || *ttp == '\n')
			{
				switch (lastCmd)
				{
				default:
					lastCmd = Nothing;
					memFrom = 0;
					memLen = 0;
					continue; /* blank line */
				case Breakpoint:
					lastCmd = Nothing;
					memFrom = 0;
					memLen = 0;
					continue;
				case Quit:
					strncpy(ttybuf, "quit", sizeof(ttybuf));
					break;
				case Step:
					strncpy(ttybuf, "step", sizeof(ttybuf));
					break;
				case Memory:
					memFrom += memLen;
					snprintf(ttybuf, sizeof(ttybuf), "memory %X %X", memFrom, memLen);
					break;
				case Registers:
					strncpy(ttybuf, "registers", sizeof(ttybuf));
					break;
				case Verbose:
					strncpy(ttybuf, "verbose", sizeof(ttybuf));
					break;
				case Help:
					strncpy(ttybuf, "Help", sizeof(ttybuf));
					break;
				case Continue:
					strncpy(ttybuf, "continue", sizeof(ttybuf));
					break;
				case Run:
					strncpy(ttybuf, "run", sizeof(ttybuf));
					break;
				}
			}
			token[0] = 0;
			if ( (args=sscanf(ttp, "%127s", token)) != 1 )
			{
				fprintf(stderr, "Invalid characters;  unrecognized command. args=%d, token='%s'\n", args, token);
				continue;
			}
			if ( !strncasecmp(token,"step",strlen(token)) )
			{
				lastCmd = Step;
				if ( !asap->cannotContinue )
				{
					if ( asap->breakPointSet && asap->pcQue[0] == asap->breakPoint )
					{
						const HashEntry_t *he = findHash(asap,asap->breakPoint);
						printf("Hit breakpoint at %08X", asap->breakPoint);
						if ( he )
							printf(": %s", he->name);
						printf("\nBefore execution:\n");
						dumpRegs(asap);
						printf("Executing instruction at breakpoint address\n");
					}
					asap->cannotContinue = executeInstruction(asap);
					if ( asap->cannotContinue || asap->verbose )
					{
						if ( asap->showText[0] )
						{
							fputs(asap->showText,stdout);
							if ( !strchr(asap->showText,'\n') )
								fputs("\n",stdout);
						}
					}
					if ( asap->errorMsg[0] )
					{
						fputs(asap->errorMsg,stdout);
						if ( !strchr(asap->errorMsg,'\n') )
							fputs("\n",stdout);
					}
				}
				else
					lastCmd = Nothing;
				continue;
			}
			if ( !strncasecmp(token,"breakpoint",strlen(token)) || !strncasecmp(token,"bp",strlen(token)))
			{
				const HashEntry_t *he;
				char *endp;
				
				lastCmd = Breakpoint;
				endp = ttybuf+strlen(token);
				while ( isspace(*endp) )
					++endp;
				if ( *endp )
				{
					asap->breakPointSet = false;
					asap->breakPoint = 0;
					token[0] = 0;
					if ( (args=sscanf(endp, "%127s", token)) != 1 )
					{
						fprintf(stderr, "Invalid characters;  unrecognized argument to breakpoint command. args=%d, token='%s'\n", args, token);
						continue;
					}
					endp = NULL;
					asap->breakPoint = strtoul(token, &endp, 16);
					if ( !endp || *endp )
					{
						if ( !asap->numUsedHashes )
						{
							printf("No symbols available. Can't set bp to '%s'\n", token);
							continue;
						}
						he = findHashByName(asap, token);
						if ( !he )
						{
							printf("No such symbol as '%s'\n", token);
							continue;
						}
						asap->breakPoint = he->value;
						printf("Breakpoint set at %s: 0x%08X\n", he->name, he->value);
					}
					else
					{
						if ( asap->breakPoint > asap->memLen )
						{
							fprintf(stderr, "Breakpoint value 0x%08X out of memory limits 0x%08X\n",
									asap->breakPoint, asap->memLen);
							continue;
						}
						printf("Breakpoint set at 0x%08X\n", asap->breakPoint);
					}
					asap->breakPointSet = true;
				}
				else
				{
					if ( asap->breakPointSet )
					{
						he = findHash(asap,asap->breakPoint);
						printf("Breakpoint set at %08X", asap->breakPoint);
						if ( he )
							printf(": %s", he->name);
						printf("\n");
					}
					else
						printf("No breakpoint set\n");
				}
				continue;
			}
			if ( !strncasecmp(token,"registers",strlen(token)) )
			{
				lastCmd = Registers;
				dumpRegs(asap);
				continue;
			}
			if ( !strncasecmp(token,"memory",strlen(token)) )
			{
				lastCmd = Memory;
				ttp += strlen(token);
				args = sscanf(ttp,"%x %x",&memFrom,&memLen);
				if ( !args )
				{
					memFrom = 0;
					memLen = asap->memLen;
				}
				else if ( args == 1 )
				{
					memLen = asap->memLen + asap->stackSize - memFrom;
				}
				else if ( args != 2 )
				{
					fprintf(stderr,"Bad from and/or to parameters\n");
					memFrom = 0;
					memLen = 0;
					lastCmd = Nothing;
					continue;
				}
				memFrom = (memFrom+3) & -4;
				memLen = (memLen+3) & -4;
				if ( memLen > 0 )
				{
					hexDump(asap,memFrom,memLen);
/*					dumpMem(asap, memFrom, memLen); */
				}
				continue;
			}
			if ( !strncasecmp(token,"verbose",strlen(token)) )
			{
				lastCmd = Verbose;
				asap->verbose = !asap->verbose;
				printf("Verbose turned %s\n", asap->verbose ? "ON":"OFF");
				continue;
			}
			if ( !strncasecmp(token,"help",strlen(token)) )
			{
				lastCmd = Help;
				printf("Commands are:\n"
					   "(Commands can be abbreviated to 1 or more characters)\n"
					   "breakpoint n - set breakpoint at 'n' (expected to be hex)\n"
					   "             - 'n' could also be a symbol if available\n"
					   "bp        - same as breakpoint"
					   "continue  - continue execution. Have to ^C to stop and exit.\n"
					   "exit      - exit\n"
					   "memory [start [nBytes]] - display memory.\n"
					   "            optional start address\n"
					   "            followed by optional byte count\n"
					   "            Default start is 0 and count is all of memory\n"
					   "            start and nBytes are expected to be in hex\n"
					   "quit      - exit\n"
					   "registers - show registers\n"
					   "run       - continue execution. Have to ^C to stop and exit.\n"
					   "step      - execute one instruction\n"
					   "verbose   - toggle verbose mode\n"
					   );
					   
				if ( !asap->cannotContinue )
					asap->interactive = 0;
				continue;
			}
			if ( !strncasecmp(token,"continue",strlen(token)) || !strncasecmp(token,"run",strlen(token)) )
			{
				lastCmd = Continue;
				if ( !asap->cannotContinue )
					asap->interactive = 0;
				else
				{
					printf("Due to error condition, cannot continue\n");
					lastCmd = Nothing;
				}
				continue;
			}
			if ( !strncasecmp(token,"quit",strlen(token)) || !strncasecmp(token,"exit",strlen(token)) )
			{
				lastCmd = Quit;
				return;
			}
			fprintf(stderr,"Unrecognized command\n");
			continue;
		}
		if ( asap->breakPointSet && asap->pcQue[0] == asap->breakPoint )
		{
			const HashEntry_t *he = findHash(asap,asap->breakPoint);
			printf("Hit breakpoint at %08X", asap->breakPoint);
			if ( he )
				printf(": %s", he->name);
			printf("\nBefore execution:\n");
			dumpRegs(asap);
			asap->interactive = true;
			continue;
		}
		asap->cannotContinue = executeInstruction(asap);
		if ( asap->cannotContinue || asap->verbose || asap->errorMsg[0] )
		{
			if ( asap->showText[0] )
			{
				fputs(asap->showText,stdout);
				if ( !strchr(asap->showText,'\n') )
					fputs("\n",stdout);
			}
			if ( asap->errorMsg[0] )
			{
				fputs(asap->errorMsg,stdout);
				if ( !strchr(asap->errorMsg,'\n') )
					fputs("\n",stdout);
			}
		}
		if ( asap->cannotContinue )
		{
			if ( asap->verbose )
			{
				printf("After execution (%d bytes):\n", asap->memLen);
				dumpRegs(asap);
				printf("\n");
			}
			return;
		}
	}
}

