/* 한랭 런타임 (C): 소리 (ALSA). 소리내다 를 쓰는 프로그램에만 붙는다 (-lasound).
 * Makefile 이 gcc -S 로 구워 runtime/runtime-sound.s 로 (지역 라벨 .L -> .Lsn).
 *
 *   표본목록을 소리내다.    소수 목록 (-1 ~ 1, 1초에 44100개)
 *   "파일.wav"를 소리내다.   PCM wav (8·16·24·32비트 정수, 32비트 소수, 모노·스테레오, 아무 표본 빠르기)
 *
 * 안 기다린다: 소리마다 '목소리' 하나를 만들어 두면 따로 도는 스레드가 섞어서 장치로 보낸다 (겹쳐 틀 수 있음).
 * 그 스레드는 GC 메모리를 안 만진다 — 표본은 부른 쪽에서 malloc 으로 옮겨 둔다.
 * 프로그램이 끝날 때는 틀던 소리가 다 끝날 때까지 기다린다 (소리 내고 바로 끝나는 프로그램도 들리게).
 * HAN_SOUND_DEVICE 로 ALSA 장치를 바꿀 수 있다 (기본 "default").
 * HAN_SCREENSHOT(테스트·놀이터 그림)이면 소리를 안 낸다. HAN_SOUND_OUT=파일 이면 장치 대신 그 파일에
 * 소리마다 16비트 표본을 이어 붙인다 (테스트용).
 * 소리 장치를 못 열면 한 번 경고하고 소리 없이 계속한다 (게임이 소리 때문에 멈추지 않게). */
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
__attribute__((noreturn)) void __han_fail_msg(long line, const char *msg);

#define RATE 44100
#define PERIOD 512
#define VOICES 32

typedef struct { float *s; long n, pos; } Voice;
static Voice voices[VOICES];
static pthread_mutex_t vlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vcond = PTHREAD_COND_INITIALIZER;
static int state;                     /* 0 아직, 1 장치 열림, 2 소리 없음(못 엶·끔), 3 파일로 */
static snd_pcm_t *pcm;
static FILE *outf;

static void serr(long line, const char *msg) {
    fflush(stdout);
    __han_fail_msg(line, msg);
}

static int busy(void) {
    for (int i = 0; i < VOICES; i++) if (voices[i].s) return 1;
    return 0;
}

static void *mixer(void *arg) {
    (void)arg;
    float mix[PERIOD];
    int16_t out[PERIOD];
    for (;;) {
        pthread_mutex_lock(&vlock);
        while (!busy()) pthread_cond_wait(&vcond, &vlock);     /* 틀 게 없으면 잠듦 */
        memset(mix, 0, sizeof mix);
        for (int i = 0; i < VOICES; i++) {
            Voice *v = &voices[i];
            if (!v->s) continue;
            long k = v->n - v->pos;
            if (k > PERIOD) k = PERIOD;
            for (long j = 0; j < k; j++) mix[j] += v->s[v->pos + j];
            v->pos += k;
            if (v->pos >= v->n) { free(v->s); v->s = 0; }
        }
        if (!busy()) pthread_cond_broadcast(&vcond);           /* 끝날 때 기다리는 쪽 깨움 */
        pthread_mutex_unlock(&vlock);
        for (int j = 0; j < PERIOD; j++) {
            float f = mix[j];
            if (f > 1.0f) f = 1.0f;
            if (f < -1.0f) f = -1.0f;
            out[j] = (int16_t)(f * 32767.0f);
        }
        long left = PERIOD; int16_t *p = out;
        while (left > 0) {
            snd_pcm_sframes_t w = snd_pcm_writei(pcm, p, left);
            if (w < 0) { if (snd_pcm_recover(pcm, (int)w, 1) < 0) break; continue; }
            left -= w; p += w;
        }
    }
    return 0;
}

static void drain(void) {             /* 프로그램 끝: 틀던 소리가 다 끝날 때까지 */
    if (state == 1) {
        pthread_mutex_lock(&vlock);
        while (busy()) pthread_cond_wait(&vcond, &vlock);
        pthread_mutex_unlock(&vlock);
        snd_pcm_drain(pcm);
    }
    if (outf) fclose(outf);
}

static void quiet(const char *f, int l, const char *fn, int e, const char *fmt, ...) { (void)f; (void)l; (void)fn; (void)e; (void)fmt; }

static void start(void) {
    const char *shot = getenv("HAN_SCREENSHOT");
    const char *outp = getenv("HAN_SOUND_OUT");
    state = 2;
    if (outp && *outp) {
        outf = fopen(outp, "wb");
        if (outf) { state = 3; atexit(drain); }
        return;
    }
    if (shot && *shot) return;
    snd_lib_error_set_handler(quiet);                      /* ALSA 가 영어로 직접 찍는 오류는 끔 (우리 경고만) */
    const char *dev = getenv("HAN_SOUND_DEVICE");          /* 보통은 "default" (테스트는 "null") */
    int e = snd_pcm_open(&pcm, dev && *dev ? dev : "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (e >= 0) e = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 40000);
    if (e < 0) {
        fflush(stdout);
        fprintf(stderr, "[경고] 소리 장치를 못 열어서 소리 없이 계속함 (%s)\n", snd_strerror(e));
        return;
    }
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &a, mixer, 0) != 0) return;
    state = 1;
    atexit(drain);
}

/* 표본(malloc 한 것)을 넘겨받아 틀기 시작 — 다 쓰면 free */
static void play(float *s, long n) {
    if (state == 0) start();
    if (state == 3) {
        for (long i = 0; i < n; i++) {
            float f = s[i];
            if (f > 1.0f) f = 1.0f;
            if (f < -1.0f) f = -1.0f;
            int16_t v = (int16_t)(f * 32767.0f);
            fwrite(&v, 2, 1, outf);
        }
    }
    if (state != 1 || n <= 0) { free(s); return; }
    pthread_mutex_lock(&vlock);
    int slot = -1;
    for (int i = 0; i < VOICES; i++) if (!voices[i].s) { slot = i; break; }
    if (slot < 0) {                   /* 꽉 찼으면 가장 많이 튼 것을 끊음 */
        slot = 0;
        for (int i = 1; i < VOICES; i++) if (voices[i].pos > voices[slot].pos) slot = i;
        free(voices[slot].s);
    }
    voices[slot].s = s; voices[slot].n = n; voices[slot].pos = 0;
    pthread_cond_broadcast(&vcond);
    pthread_mutex_unlock(&vlock);
}

/* 표본목록을 소리내다.  list = 목록 핸들 {개수, 용량, 자료*, 열수}, 칸은 double 비트 */
void __han_sound(long *list, long line) {
    long n = list[0];
    double *d = (double *)list[2];
    float *s = malloc(sizeof(float) * (n > 0 ? n : 1));
    if (!s) serr(line, "소리 표본을 담을 메모리가 모자람");
    for (long i = 0; i < n; i++) s[i] = (float)d[i];
    play(s, n);
}

/* wav 읽기 — 같은 파일을 여러 번 틀면 처음 읽은 것을 다시 씀 */
#define CACHE 32
static struct { char *path; float *s; long n; } cache[CACHE];
static int ncache;

static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

static void wavFail(long line, const char *path, const char *why) {
    char m[600];
    snprintf(m, sizeof m, "'%s' 를 소리로 낼 수 없음 (%s)", path, why);
    serr(line, m);
}

void __han_sound_file(const char *path, long line) {
    for (int i = 0; i < ncache; i++) if (strcmp(cache[i].path, path) == 0) {
        float *s = malloc(sizeof(float) * (cache[i].n > 0 ? cache[i].n : 1));
        if (!s) serr(line, "소리 표본을 담을 메모리가 모자람");
        memcpy(s, cache[i].s, sizeof(float) * cache[i].n);
        play(s, cache[i].n);
        return;
    }
    if (getenv("HAN_PLAYGROUND") && (path[0] == '/' || strstr(path, ".."))) wavFail(line, path, "놀이터에서는 지금 폴더 안 파일만");
    FILE *f = fopen(path, "rb");
    if (!f) wavFail(line, path, "파일을 열 수 없음");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(size > 0 ? size : 1);
    if (!b || fread(b, 1, size, f) != (size_t)size) { fclose(f); free(b); wavFail(line, path, "읽다가 실패"); }
    fclose(f);
    if (size < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); wavFail(line, path, "wav 파일이 아님"); }
    unsigned fmt = 0, ch = 0, rate = 0, bits = 0;
    const unsigned char *data = 0; long dlen = 0;
    long p = 12;
    while (p + 8 <= size) {
        unsigned len = rd32(b + p + 4);
        const unsigned char *c = b + p + 8;
        if (p + 8 + (long)len > size) len = (unsigned)(size - p - 8);
        if (!memcmp(b + p, "fmt ", 4) && len >= 16) {
            fmt = rd16(c); ch = rd16(c + 2); rate = rd32(c + 4); bits = rd16(c + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = rd16(c + 24);       /* WAVE_FORMAT_EXTENSIBLE */
        } else if (!memcmp(b + p, "data", 4)) { data = c; dlen = len; }
        p += 8 + len + (len & 1);
    }
    if (!data || !ch || !rate) { free(b); wavFail(line, path, "fmt·data 덩어리가 없음"); }
    if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) {
        free(b); wavFail(line, path, "PCM(8·16·24·32비트) 이나 32비트 소수 wav 만 됨");
    }
    long bps = bits / 8, frames = dlen / (bps * ch);
    float *mono = malloc(sizeof(float) * (frames > 0 ? frames : 1));
    for (long i = 0; i < frames; i++) {
        float sum = 0;
        for (unsigned k = 0; k < ch; k++) {
            const unsigned char *q = data + (i * ch + k) * bps;
            float v;
            if (fmt == 3) { float x; memcpy(&x, q, 4); v = x; }
            else if (bits == 8) v = (q[0] - 128) / 128.0f;
            else if (bits == 16) v = (int16_t)rd16(q) / 32768.0f;
            else if (bits == 24) v = (float)((int32_t)((unsigned)q[0] << 8 | (unsigned)q[1] << 16 | (unsigned)q[2] << 24) >> 8) / 8388608.0f;
            else v = (int32_t)rd32(q) / 2147483648.0f;
            sum += v;
        }
        mono[i] = sum / ch;
    }
    free(b);
    long n = frames;
    float *s = mono;
    if (rate != RATE && frames > 0) {  /* 표본 빠르기가 다르면 사이를 직선으로 이어 44100 으로 */
        n = (long)((double)frames * RATE / rate);
        s = malloc(sizeof(float) * (n > 0 ? n : 1));
        for (long i = 0; i < n; i++) {
            double t = (double)i * rate / RATE;
            long a = (long)t;
            double fr = t - a;
            float x = mono[a < frames ? a : frames - 1], y = mono[a + 1 < frames ? a + 1 : frames - 1];
            s[i] = (float)(x + (y - x) * fr);
        }
        free(mono);
    }
    if (ncache < CACHE) {
        cache[ncache].path = strdup(path);
        cache[ncache].s = s; cache[ncache].n = n;
        ncache++;
        float *c = malloc(sizeof(float) * (n > 0 ? n : 1));
        memcpy(c, s, sizeof(float) * n);
        play(c, n);
    } else play(s, n);
}
