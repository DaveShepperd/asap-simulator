#ifndef _LCLREADLINE_H_
#define _LCLREADLINE_H_ 1

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

extern int lclReadLine(void *fin, const char *prompt, char *buff, int buflen);
extern void lclPurgeReadLineHistory(void);

#endif	/* _LCLREADLINE_H_ */
