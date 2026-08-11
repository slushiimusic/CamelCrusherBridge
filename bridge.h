#ifndef BRIDGE_H
#define BRIDGE_H
#include <pthread.h>
#include <unistd.h>
#include "vst2.h"
#include "bridge_common.h"

typedef struct Bridge {
    BridgeSHM*      shm;
    pid_t           pid;
    char            shmname[64];
    pthread_mutex_t lock;
    int             ok;
    int             curProgram;
    AEffect         eff;
    void*           editor;      /* CCEditor* (Obj-C), opaque here */
    AudioMasterProc master;
} Bridge;

int   bridge_start(Bridge* b);
void  bridge_stop(Bridge* b);
int   bridge_call(Bridge* b, int cmd, int timeout_us);
void  bridge_set_param(Bridge* b, int index, float v);
float bridge_get_param(Bridge* b, int index);

void  bridge_set_samplerate(Bridge* b, double sr);
void  bridge_set_blocksize(Bridge* b, int n);
void  bridge_set_mains(Bridge* b, int on);
void  bridge_set_program(Bridge* b, int p);
const char* bridge_program_name(Bridge* b, int idx);
const void* bridge_get_chunk(Bridge* b, int* outSize);
int   bridge_set_chunk(Bridge* b, const void* data, int size);
void  bridge_process(Bridge* b, float* const* in, float* const* out, int n);
const char* cc_param_name(int i);

/* GUI entry points implemented in gui.m */
void* cc_editor_create(Bridge* b, void* parentNSView);
void* cc_editor_create_standalone(Bridge* b);
void  cc_editor_destroy(void* editor);
void  cc_editor_refresh(void* editor);

#define CC_UI_W 345
#define CC_UI_H 373

#endif
