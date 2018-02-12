#ifndef _DIME_PY_H
#define _DIME_PY_H 

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

int PY_Init(const char *arg);
void PY_Reset(void);
int PY_GetCandWords(int mode);
int PY_Destroy(void);
int PY_DoInput(int key);

struct _EIM {
	/* interface defined by platform */
	int CandWordMax;
	int CodeLen;
	int CandWordCount;
	int CandPageCount;
	int CaretPos;
	char CodeInput[256];
	char StringGet[256];
	
};

extern struct _EIM EIM;

#ifdef __cplusplus
}
#endif

#endif /* ifndef _DIME_PY_H */

