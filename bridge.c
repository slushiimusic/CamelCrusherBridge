// Shared IPC client: spawns and drives the x86_64 helper that hosts the
// original Intel plugin. Linked by both the VST2 and AU front-ends.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include "bridge.h"

/* Canonical location of the untouched Intel plugin. Never point this at an
   install slot: after replacement those paths hold the arm64 bridge. */
#define CC_ORIGINAL_VST \
  "/Library/Application Support/Camel Audio/CamelCrusherOriginal.vst/Contents/MacOS/CamelCrusher"

extern char** environ;

static uint64_t now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}
static void nap(long us) {
    struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}

/* Directory containing this dylib, i.e. <bundle>/Contents/MacOS */
static int self_dir(char* out, size_t n) {
    Dl_info info;
    if (!dladdr((void*)self_dir, &info) || !info.dli_fname) return 0;
    snprintf(out, n, "%s", info.dli_fname);
    char* slash = strrchr(out, '/');
    if (!slash) return 0;
    *slash = 0;
    return 1;
}

const char* cc_param_name(int i) {
    static const char* names[] = {
        "Dist On","Mech","Tube","Filter On","Cutoff","Reso","Comp On",
        "Comp Amt","Phat Mode","Master On","Mix","Volume",
        "Unused1","Unused2","Unused3","Unused4","Unused5" };
    return (i >= 0 && i < 17) ? names[i] : "";
}

int bridge_call(Bridge* b, int cmd, int timeout_us) {
    if (!b->ok || !b->shm) return 0;
    BridgeSHM* s = b->shm;
    s->cmd = cmd;
    uint32_t seq = atomic_fetch_add(&s->reqSeq, 1) + 1;
    uint64_t deadline = now_us() + (uint64_t)timeout_us;
    long spins = 0;
    for (;;) {
        if (atomic_load(&s->ackSeq) == seq) return 1;
        if (atomic_load(&s->helperFailed)) { b->ok = 0; return 0; }
        if (++spins < 8000) { __asm__ __volatile__("yield"); continue; }
        if (now_us() > deadline) { b->ok = 0; return 0; }
        nap(50);
    }
}

void bridge_set_param(Bridge* b, int index, float v) {
    if (!b->shm || index < 0 || index >= BRIDGE_MAX_PARAM) return;
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    b->shm->params[index] = v;
    atomic_fetch_or(&b->shm->paramDirty, 1u << index);
}

float bridge_get_param(Bridge* b, int index) {
    if (!b->shm || index < 0 || index >= BRIDGE_MAX_PARAM) return 0.f;
    return b->shm->params[index];
}

/* --- convenience wrappers used by both front-ends (all take the lock) --- */

void bridge_set_samplerate(Bridge* b, double sr) {
    if (!b || !b->ok) return;
    pthread_mutex_lock(&b->lock);
    b->shm->darg = sr;
    bridge_call(b, CMD_SETSR, 500000);
    pthread_mutex_unlock(&b->lock);
}

void bridge_set_blocksize(Bridge* b, int n) {
    if (!b || !b->ok) return;
    pthread_mutex_lock(&b->lock);
    b->shm->iarg = n;
    bridge_call(b, CMD_SETBLOCK, 500000);
    pthread_mutex_unlock(&b->lock);
}

void bridge_set_mains(Bridge* b, int on) {
    if (!b || !b->ok) return;
    pthread_mutex_lock(&b->lock);
    b->shm->iarg = on;
    bridge_call(b, CMD_MAINS, 500000);
    pthread_mutex_unlock(&b->lock);
}

void bridge_set_program(Bridge* b, int p) {
    if (!b || !b->ok) return;
    pthread_mutex_lock(&b->lock);
    b->shm->iarg = p;
    bridge_call(b, CMD_SETPROGRAM, 500000);
    pthread_mutex_unlock(&b->lock);
    b->curProgram = p;
}

const char* bridge_program_name(Bridge* b, int idx) {
    static char buf[BRIDGE_STRLEN];
    buf[0] = 0;
    if (!b || !b->ok) return buf;
    pthread_mutex_lock(&b->lock);
    b->shm->iarg = idx;
    if (bridge_call(b, CMD_PROGNAME, 500000)) {
        strncpy(buf, b->shm->str, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
    }
    pthread_mutex_unlock(&b->lock);
    return buf;
}

const void* bridge_get_chunk(Bridge* b, int* outSize) {
    *outSize = 0;
    if (!b || !b->ok) return NULL;
    pthread_mutex_lock(&b->lock);
    b->shm->iarg = 0;                       /* full bank */
    const void* p = NULL;
    if (bridge_call(b, CMD_GETCHUNK, 2000000)) {
        *outSize = b->shm->chunkSize;
        p = b->shm->chunk;
    }
    pthread_mutex_unlock(&b->lock);
    return p;
}

int bridge_set_chunk(Bridge* b, const void* data, int size) {
    if (!b || !b->ok || !data || size <= 0 || size > BRIDGE_MAX_CHUNK) return 0;
    pthread_mutex_lock(&b->lock);
    memcpy(b->shm->chunk, data, (size_t)size);
    b->shm->chunkSize = size;
    b->shm->iarg = 0;
    int ok = bridge_call(b, CMD_SETCHUNK, 2000000);
    pthread_mutex_unlock(&b->lock);
    return ok;
}

/* Real-time path. Falls back to passthrough rather than blocking or glitching. */
void bridge_process(Bridge* b, float* const* in, float* const* out, int n) {
    if (n <= 0) return;
    if (n > BRIDGE_MAX_BLOCK) n = BRIDGE_MAX_BLOCK;
    size_t bytes = sizeof(float) * (size_t)n;

    if (!b || !b->ok || pthread_mutex_trylock(&b->lock) != 0) {
        for (int c = 0; c < 2; c++)
            if (out[c] != in[c]) memcpy(out[c], in[c], bytes);
        return;
    }
    BridgeSHM* s = b->shm;
    memcpy(s->audio[0], in[0], bytes);
    memcpy(s->audio[1], in[1], bytes);
    s->nframes = n;
    if (bridge_call(b, CMD_PROCESS, 100000)) {
        memcpy(out[0], s->audio[2], bytes);
        memcpy(out[1], s->audio[3], bytes);
    } else {
        for (int c = 0; c < 2; c++)
            if (out[c] != in[c]) memcpy(out[c], in[c], bytes);
    }
    pthread_mutex_unlock(&b->lock);
}

int bridge_start(Bridge* b) {
    static int counter = 0;
    pthread_mutex_init(&b->lock, NULL);
    snprintf(b->shmname, sizeof(b->shmname), "/ccbridge.%d.%d", getpid(), counter++);
    shm_unlink(b->shmname);

    int fd = shm_open(b->shmname, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) return 0;
    if (ftruncate(fd, sizeof(BridgeSHM)) != 0) { close(fd); shm_unlink(b->shmname); return 0; }
    b->shm = mmap(NULL, sizeof(BridgeSHM), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (b->shm == MAP_FAILED) { b->shm = NULL; shm_unlink(b->shmname); return 0; }

    memset(b->shm, 0, sizeof(BridgeSHM));
    b->shm->magic = BRIDGE_MAGIC;
    b->shm->structSize = (uint32_t)sizeof(BridgeSHM);

    char dir[1024], helper[1100], vst[1400];
    if (!self_dir(dir, sizeof(dir))) return 0;
    snprintf(helper, sizeof(helper), "%s/CamelCrusherHelper", dir);
    snprintf(vst, sizeof(vst), "%s/../Resources/CamelCrusher.vst/Contents/MacOS/CamelCrusher", dir);
    if (access(vst, R_OK) != 0)
        snprintf(vst, sizeof(vst), "%s", CC_ORIGINAL_VST);

    char* argv[] = { helper, b->shmname, vst, NULL };
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    /* Helper is x86_64-only; the kernel routes it through Rosetta automatically. */
    if (posix_spawn(&b->pid, helper, NULL, &attr, argv, environ) != 0) {
        posix_spawnattr_destroy(&attr);
        return 0;
    }
    posix_spawnattr_destroy(&attr);

    uint64_t deadline = now_us() + 15000000ull;   /* Rosetta first-run can be slow */
    while (now_us() < deadline) {
        if (atomic_load(&b->shm->helperReady)) { b->ok = 1; return 1; }
        if (atomic_load(&b->shm->helperFailed)) break;
        int st;
        if (waitpid(b->pid, &st, WNOHANG) == b->pid) break;
        nap(2000);
    }
    return 0;
}

void bridge_stop(Bridge* b) {
    if (b->shm && b->ok) bridge_call(b, CMD_QUIT, 200000);
    if (b->pid > 0) {
        uint64_t deadline = now_us() + 1000000ull;
        int st;
        while (now_us() < deadline) {
            if (waitpid(b->pid, &st, WNOHANG) == b->pid) { b->pid = 0; break; }
            nap(5000);
        }
        if (b->pid > 0) { kill(b->pid, SIGKILL); waitpid(b->pid, &st, 0); b->pid = 0; }
    }
    if (b->shm) { munmap(b->shm, sizeof(BridgeSHM)); b->shm = NULL; }
    if (b->shmname[0]) shm_unlink(b->shmname);
    b->ok = 0;
    pthread_mutex_destroy(&b->lock);
}
