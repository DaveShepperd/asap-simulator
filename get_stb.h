#ifndef _GET_STB_H_
#define _GET_STB_H_

#include "asapExecute.h"

extern int get_stb(Asap_t *asap);
extern const HashEntry_t *findHash(Asap_t *asap, uint32_t value);
extern const HashEntry_t *findHashByName(Asap_t *asap, const char *name);

#endif	/* _GET_STB_H_ */
