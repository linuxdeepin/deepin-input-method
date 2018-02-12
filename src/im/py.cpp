#include "py.h"

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <pinyin.h>

#include "py.h"

#define TRACE(fmt, ...) do { \
    fprintf(stderr, "%s" #fmt "\n", __func__, ##__VA_ARGS__); \
} while (0)

static struct PYContext {
    pinyin_context_t *py_ctx;
    pinyin_instance_t * py_instance;
} CTX;

struct _EIM EIM;

int PY_Init(const char *arg)
{
    TRACE();
    CTX.py_ctx = pinyin_init("/usr/lib/x86_64-linux-gnu/libpinyin/data", "/home/sonald/.yong/data");

    pinyin_option_t options = PINYIN_CORRECT_ALL | USE_DIVIDED_TABLE | USE_RESPLIT_TABLE | DYNAMIC_ADJUST;
    pinyin_set_options(CTX.py_ctx, options);
    CTX.py_instance = pinyin_alloc_instance(CTX.py_ctx);

    memset(&EIM, 0, sizeof(EIM));
    EIM.CandWordMax = 10;
    return 0;
}

void PY_Reset(void)
{
    TRACE();
}

static int CloudMoveCaretTo(int key)
{
	int i,p;
	if(EIM.CodeLen<1)
		return 0;
	if(key>='A' && key<='Z')
		key='a'+(key-'A');
	for(i=0,p=EIM.CaretPos+1;i<EIM.CodeLen;i++)
	{
		if(p&0x01)
			p++;
		if(p>=EIM.CodeLen)
			p=0;
		if(EIM.CodeInput[p]==key)
		{
			EIM.CaretPos=p;
			break;
		}
		p++;
	}
	return 0;
}

int PY_GetCandWords(int mode)
{
    TRACE();

    pinyin_parse_more_full_pinyins(CTX.py_instance, EIM.CodeInput);
    pinyin_guess_sentence_with_prefix(CTX.py_instance, "");
    pinyin_guess_full_pinyin_candidates(CTX.py_instance, 0);

    guint len = 0;
    pinyin_get_n_candidate(CTX.py_instance, &len);

    EIM.CandWordCount = MIN(len, EIM.CandWordMax);
    EIM.CandPageCount = len / EIM.CandWordMax + (len % EIM.CandWordMax > 0);

    for (size_t i = 0; i < EIM.CandWordCount; ++i) {
        lookup_candidate_t * candidate = NULL;
        pinyin_get_candidate(CTX.py_instance, i, &candidate);

        const char* word = NULL;
        pinyin_get_candidate_string(CTX.py_instance, candidate, &word);
        if (i == 0) {
            strcpy(EIM.StringGet, word);
        }
        printf("%s\t", word);
    }

    printf("\n");

    //pinyin_train(CTX.py_instance);
    //pinyin_reset(CTX.py_instance);
    //pinyin_save(CTX.py_ctx);
    return 0;
}

int PY_Destroy(void)
{
    TRACE();

    pinyin_free_instance(CTX.py_instance);
    CTX.py_instance = NULL;

    pinyin_mask_out(CTX.py_ctx, 0x0, 0x0);
    pinyin_save(CTX.py_ctx);
    pinyin_fini(CTX.py_ctx);
    CTX.py_ctx = NULL;
    return 0;
}

int PY_DoInput(int key)
{
    TRACE("%c", key);
    if (key >= 'a' && key <= 'z') {
        EIM.CodeInput[EIM.CaretPos++] = key;
        EIM.CodeLen++;
        EIM.CodeInput[EIM.CodeLen] = 0;
        EIM.CaretPos = EIM.CodeLen;

        PY_GetCandWords(0);
        return 0;

    } else if (key>='A' && key<='Z') {
		if(EIM.CodeLen>=1)
			CloudMoveCaretTo(key);
    }
    return 0;
}

