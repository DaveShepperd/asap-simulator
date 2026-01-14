/* QA.H -- header for using the QA Question & Answer routine(s) */
#ifndef _QA_H_
#define _QA_H_

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

extern int qa5(FILE *fin, FILE *fout, const char *prompt, int bufsiz, char *buf);
extern void purge_qa2(FILE *fin, FILE *fout);
extern void purgeHistory(void);
extern int _qaval_; /* global cell for (guaranteed) returned value */
extern int _qacnt_; /* global cell for number of characters read */

#define qa( prompt, bufsiz, buf ) qa5( stdin, stdout, prompt, bufsiz, buf )
#define purge_qa() purge_qa2( stdin, stdout )

#endif	/* _QA_H_ */

