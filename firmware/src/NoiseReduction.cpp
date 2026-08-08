#include "NoiseReduction.h"
#include <math.h>
#include <string.h>

#define NR_N      256
#define NR_HOP    128
#define NR_NB     (NR_N / 2 + 1)
#define NR_OVERS  2.0f
#define NR_FLOOR  0.015f
#define NR_NOISE_A  0.4f

static float nr_hann[NR_N];
static float nr_tw_re[NR_N / 2 + 1];
static float nr_tw_im[NR_N / 2 + 1];
static float nr_in[NR_N];
static float nr_noise[NR_NB];
static float nr_ola[NR_N - NR_HOP];
static int nr_tw_ok = 0;
static int nr_nframes = 0;
static float nr_noise_total = 0.0f;

static void nr_fft(float* re, float* im)
{
    int n = NR_N;
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float wl_re = nr_tw_re[len >> 1];
        float wl_im = nr_tw_im[len >> 1];
        for (int i = 0; i < n; i += len) {
            float wr = 1.0f, wi = 0.0f;
            int half = len >> 1;
            for (int j = 0; j < half; j++) {
                float u_re = re[i + j], u_im = im[i + j];
                float v_re = re[i + j + half] * wr - im[i + j + half] * wi;
                float v_im = re[i + j + half] * wi + im[i + j + half] * wr;
                re[i + j] = u_re + v_re;
                im[i + j] = u_im + v_im;
                re[i + j + half] = u_re - v_re;
                im[i + j + half] = u_im - v_im;
                float nwr = wr * wl_re - wi * wl_im;
                wi = wr * wl_im + wi * wl_re;
                wr = nwr;
            }
        }
    }
}

static void nr_ifft(float* re, float* im)
{
    for (int i = 0; i < NR_N; i++) im[i] = -im[i];
    nr_fft(re, im);
    for (int i = 0; i < NR_N; i++) { re[i] /= NR_N; im[i] = -im[i] / NR_N; }
}

static void nr_init_tw(void)
{
    for (int len = 2; len <= NR_N; len <<= 1) {
        float a = (float)(-2.0 * M_PI / len);
        nr_tw_re[len >> 1] = cosf(a);
        nr_tw_im[len >> 1] = sinf(a);
    }
    for (int i = 0; i < NR_N; i++) {
        nr_hann[i] = 0.5f - 0.5f * cosf((float)(2.0 * M_PI * i / (NR_N - 1)));
    }
    nr_tw_ok = 1;
}

void nr_init(void)
{
    if (!nr_tw_ok) nr_init_tw();
    memset(nr_in, 0, sizeof(nr_in));
    memset(nr_noise, 0, sizeof(nr_noise));
    memset(nr_ola, 0, sizeof(nr_ola));
    nr_nframes = 0;
    nr_noise_total = 0.0f;
}

void nr_process(const int16_t* in, int16_t* out, int n)
{
    static float re[NR_N], im[NR_N];
    if (!nr_tw_ok) nr_init_tw();
    if (n != NR_HOP) return;

    memmove(nr_in, nr_in + NR_HOP, (NR_N - NR_HOP) * sizeof(float));
    for (int i = 0; i < NR_HOP; i++) nr_in[NR_N - NR_HOP + i] = (float)in[i] / 32768.0f;

    for (int i = 0; i < NR_N; i++) {
        re[i] = nr_in[i] * nr_hann[i];
        im[i] = 0.0f;
    }
    nr_fft(re, im);

    float total = 0.0f;
    for (int k = 0; k < NR_NB; k++) total += re[k] * re[k] + im[k] * im[k];

    if (nr_nframes < 120) {
        for (int k = 0; k < NR_NB; k++) nr_noise[k] += (re[k] * re[k] + im[k] * im[k]) / 120.0f;
        nr_noise_total = 0.0f;
        for (int k = 0; k < NR_NB; k++) nr_noise_total += nr_noise[k];
    } else if (total < 3.0f * nr_noise_total) {
        for (int k = 0; k < NR_NB; k++) {
            float p = re[k] * re[k] + im[k] * im[k];
            nr_noise[k] = (1.0f - NR_NOISE_A) * nr_noise[k] + NR_NOISE_A * p;
        }
        nr_noise_total = 0.0f;
        for (int k = 0; k < NR_NB; k++) nr_noise_total += nr_noise[k];
    }
    nr_nframes++;

    for (int k = 0; k < NR_NB; k++) {
        float p = re[k] * re[k] + im[k] * im[k];
        float g = 1.0f - NR_OVERS * nr_noise[k] / (p + 1e-8f);
        if (g < NR_FLOOR) g = NR_FLOOR;
        if (g > 1.0f) g = 1.0f;
        if (k == 0 || k == NR_NB - 1) g = NR_FLOOR;   /* DC and Nyquist heavily gated */
        re[k] *= g;
        im[k] *= g;
    }

    nr_ifft(re, im);

    for (int i = 0; i < NR_N; i++) {
        re[i] *= nr_hann[i];
    }
    for (int k = 0; k < NR_HOP; k++) {
        float v = (nr_ola[k] + re[k]) / 1.5f;
        int32_t s = (int32_t)(v * 32768.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[k] = (int16_t)s;
    }
    for (int k = 0; k < NR_N - NR_HOP; k++) {
        nr_ola[k] = re[k + NR_HOP];
    }
}
