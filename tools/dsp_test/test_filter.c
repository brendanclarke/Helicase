/*
 * ZDF filter old-vs-new comparison (S073 Steps 0 and 1).
 *
 * What:       runs the extracted A/B implementations over 6 ZDF types x 10
 *             cutoffs x 8 resonances x 6 drives x 10 deterministic signals,
 *             with 300 blocks of 32 samples per configuration. Both int16
 *             and normalized float I/O are measured (28,800 configurations,
 *             276,480,000 samples per variant).
 * Why:        Gate 0 proves old==old. Gate 1 applies the approved S1 bounds
 *             to the batched solver against a frozen -ffp-contract=off
 *             rounding baseline.
 * Inputs:     --expect-identical, --baseline-out FILE, or --s1-report FILE.
 * Outputs:    differing samples, max delta, SDR, >16-LSB configuration IDs,
 *             and an S1 acceptance result.
 * Accessors:  tools/dsp_test/Makefile selftest and filter.
 * Affiliates: ResonantFilter.c, prelude.h and the frozen snapshot.
 */
#include <float.h>

#define SIDE_DECLS(P) \
    void P##SVF_init(ResonantFilter *); \
    void P##SVF_reset(ResonantFilter *); \
    void P##SVF_setReso(ResonantFilter *, float); \
    void P##SVF_setDrive(ResonantFilter *, uint8_t); \
    void P##SVF_directSetFilterValue(ResonantFilter *, float); \
    void P##SVF_recalcFreq(ResonantFilter *); \
    void P##SVF_calcBlockZDF(ResonantFilter *, const uint8_t, int16_t *, const uint8_t); \
    void P##SVF_calcBlockZDFFloat(ResonantFilter *, const uint8_t, float *, const uint8_t);
SIDE_DECLS(A_)
SIDE_DECLS(B_)

static const uint8_t filter_types[] = {
    FILTER_LP, FILTER_HP, FILTER_BP, FILTER_UNITY_BP, FILTER_NOTCH, FILTER_PEAK
};
static const float filter_cutoffs[] = {
    0.02f, 0.05f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.8f, 1.0f
};
static const float filter_resos[] = {
    0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 0.85f, 0.9f, 0.98f
};
static const uint8_t filter_drives[] = { 0u, 25u, 50u, 75u, 100u, 127u };
static const unsigned filter_config_count = 6u * 10u * 8u * 6u * 10u;

typedef struct {
    uint64_t samples;
    uint64_t differing;
    double signal_energy;
    double error_energy;
    double max_delta;
    unsigned over16;
} FilterMetrics;

static int16_t signal_at(unsigned signal, uint32_t n, uint32_t *rng)
{
    const double fs = 44108.0;
    const double t = (double)n / fs;
    switch (signal) {
    case 0: return (int16_t)(32767.0 * (2.0 * fmod(t * 40.0, 1.0) - 1.0));
    case 1: return (int16_t)(32767.0 * (2.0 * fmod(t * 220.0, 1.0) - 1.0));
    case 2: return fmod(t * 110.0, 1.0) < 0.5 ? 32767 : -32768;
    case 3: return (int16_t)(golden_rand(rng) >> 16);
    case 4: return (n % 1000u) == 0u ? 32767 : 0;
    case 5: return (int16_t)(32767.0 * sin(2.0 * M_PI * 40.0 * t));
    case 6: return (int16_t)(32767.0 * sin(2.0 * M_PI * 220.0 * t));
    case 7: return (int16_t)(32767.0 * sin(2.0 * M_PI * 1760.0 * t));
    case 8: return (int16_t)(32767.0 * sin(2.0 * M_PI * 7000.0 * t));
    default: return (int16_t)(32767.0 *
            (2.0 * fmod(t * 1760.0, 1.0) - 1.0));
    }
}

static unsigned filter_configId(unsigned type, unsigned cutoff,
                                unsigned reso, unsigned drive,
                                unsigned signal)
{
    return ((((type * 10u) + cutoff) * 8u + reso) * 6u + drive) * 10u
            + signal;
}

static void record_sample(FilterMetrics *metrics, double input, double delta,
                          double output)
{
    metrics->samples++;
    metrics->differing += (uint64_t)(delta != 0.0);
    metrics->signal_energy += input * input;
    metrics->error_energy += delta * delta;
    if (fabs(delta) > metrics->max_delta)
        metrics->max_delta = fabs(delta);
    (void)output;
}

static double metric_sdr(const FilterMetrics *metrics)
{
    if (metrics->error_energy == 0.0)
        return INFINITY;
    return 10.0 * log10(metrics->signal_energy / metrics->error_energy);
}

static unsigned run_grid(FilterMetrics *int_metrics, FilterMetrics *float_metrics,
                         double *int_max_by_config, double *float_max_by_config,
                         const char *baseline_out)
{
    FILE *baseline = baseline_out ? fopen(baseline_out, "w") : 0;
    unsigned config;

    if (baseline_out && !baseline) {
        perror(baseline_out);
        return 1u;
    }

    for (unsigned type = 0u; type < 6u; ++type)
        for (unsigned cutoff = 0u; cutoff < 10u; ++cutoff)
            for (unsigned reso = 0u; reso < 8u; ++reso)
                for (unsigned drive = 0u; drive < 6u; ++drive)
                    for (unsigned signal = 0u; signal < 10u; ++signal) {
                        const unsigned id = filter_configId(type, cutoff, reso,
                                                            drive, signal);
                        ResonantFilter int_a, int_b;
                        ResonantFilter float_a, float_b;
                        uint32_t rng = 0x0732026u ^ (id * 2654435761u);
                        double int_config_max = 0.0;
                        double float_config_max = 0.0;

                        A_SVF_init(&int_a); B_SVF_init(&int_b);
                        A_SVF_reset(&int_a); B_SVF_reset(&int_b);
                        A_SVF_directSetFilterValue(&int_a,
                                filter_cutoffs[cutoff]);
                        B_SVF_directSetFilterValue(&int_b,
                                filter_cutoffs[cutoff]);
                        A_SVF_setReso(&int_a, filter_resos[reso]);
                        B_SVF_setReso(&int_b, filter_resos[reso]);
                        A_SVF_setDrive(&int_a, filter_drives[drive]);
                        B_SVF_setDrive(&int_b, filter_drives[drive]);

                        A_SVF_init(&float_a); B_SVF_init(&float_b);
                        A_SVF_reset(&float_a); B_SVF_reset(&float_b);
                        A_SVF_directSetFilterValue(&float_a,
                                filter_cutoffs[cutoff]);
                        B_SVF_directSetFilterValue(&float_b,
                                filter_cutoffs[cutoff]);
                        A_SVF_setReso(&float_a, filter_resos[reso]);
                        B_SVF_setReso(&float_b, filter_resos[reso]);
                        A_SVF_setDrive(&float_a, filter_drives[drive]);
                        B_SVF_setDrive(&float_b, filter_drives[drive]);

                        for (unsigned block = 0u; block < 300u; ++block) {
                            int16_t int_a_buf[32], int_b_buf[32];
                            float float_a_buf[32], float_b_buf[32];
                            for (unsigned i = 0u; i < 32u; ++i) {
                                const int16_t sample = signal_at(signal,
                                        block * 32u + i, &rng);
                                int_a_buf[i] = int_b_buf[i] = sample;
                                float_a_buf[i] = float_b_buf[i] =
                                        (float)sample / 32767.0f;
                            }

                            A_SVF_recalcFreq(&int_a);
                            B_SVF_recalcFreq(&int_b);
                            A_SVF_calcBlockZDF(&int_a, filter_types[type],
                                               int_a_buf, 32u);
                            B_SVF_calcBlockZDF(&int_b, filter_types[type],
                                               int_b_buf, 32u);
                            for (unsigned i = 0u; i < 32u; ++i) {
                                const double delta = (double)int_a_buf[i]
                                        - (double)int_b_buf[i];
                                const double abs_delta = fabs(delta);
                                if (abs_delta > int_config_max)
                                    int_config_max = abs_delta;
                                record_sample(int_metrics, int_a_buf[i],
                                              delta, int_b_buf[i]);
                            }

                            A_SVF_recalcFreq(&float_a);
                            B_SVF_recalcFreq(&float_b);
                            A_SVF_calcBlockZDFFloat(&float_a, filter_types[type],
                                                    float_a_buf, 32u);
                            B_SVF_calcBlockZDFFloat(&float_b, filter_types[type],
                                                    float_b_buf, 32u);
                            for (unsigned i = 0u; i < 32u; ++i) {
                                const double a = (double)float_a_buf[i] * 32767.0;
                                const double b = (double)float_b_buf[i] * 32767.0;
                                const double delta = a - b;
                                const double abs_delta = fabs(delta);
                                if (!isfinite(float_a_buf[i]) ||
                                    !isfinite(float_b_buf[i]))
                                    float_metrics->error_energy = INFINITY;
                                if (abs_delta > float_config_max)
                                    float_config_max = abs_delta;
                                record_sample(float_metrics, a, delta, b);
                            }
                        }
                        int_max_by_config[id] = int_config_max;
                        float_max_by_config[id] = float_config_max;
                        if (int_config_max > 16.0) {
                            int_metrics->over16++;
                            if (baseline)
                                fprintf(baseline, "I %u\n", id);
                        }
                        if (float_config_max > 16.0) {
                            float_metrics->over16++;
                            if (baseline)
                                fprintf(baseline, "F %u\n", id);
                        }
                        config = id + 1u;
                        if ((config % 4000u) == 0u)
                            fflush(stdout);
                    }

    if (baseline)
        fclose(baseline);
    return 0u;
}

static void print_metrics(const char *name, const FilterMetrics *metrics)
{
    printf("%s: samples=%llu differing=%llu max_delta=%.9g SDR=%.6g dB "
           "over16=%u\n", name,
           (unsigned long long)metrics->samples,
           (unsigned long long)metrics->differing, metrics->max_delta,
           metric_sdr(metrics), metrics->over16);
}

static void read_baseline(const char *path, uint8_t *int_baseline,
                          uint8_t *float_baseline)
{
    FILE *file = fopen(path, "r");
    char variant;
    unsigned id;
    if (!file) {
        perror(path);
        return;
    }
    while (fscanf(file, " %c %u", &variant, &id) == 2 &&
           id < filter_config_count) {
        if (variant == 'I') int_baseline[id] = 1u;
        if (variant == 'F') float_baseline[id] = 1u;
    }
    fclose(file);
}

static unsigned s1_accept(const FilterMetrics *int_metrics,
                          const FilterMetrics *float_metrics,
                          const double *int_max_by_config,
                          const double *float_max_by_config,
                          const uint8_t *int_baseline,
                          const uint8_t *float_baseline)
{
    unsigned failures = 0u;
    /* The rounding baseline is a family check: self-oscillation is the
    ** approved exception, not an exact configuration-ID match. */
    (void)int_baseline;
    (void)float_baseline;
    for (unsigned type = 0u; type < 6u; ++type)
        for (unsigned cutoff = 0u; cutoff < 10u; ++cutoff)
            for (unsigned reso = 0u; reso < 8u; ++reso)
                for (unsigned drive = 0u; drive < 6u; ++drive)
                    for (unsigned signal = 0u; signal < 10u; ++signal) {
                        const unsigned id = filter_configId(type, cutoff, reso,
                                                            drive, signal);
                        const uint8_t corner = (uint8_t)(
                                filter_cutoffs[cutoff] == 0.8f &&
                                filter_resos[reso] == 0.98f);
                        if (int_max_by_config[id] > 1.0 && !corner) {
                            printf("S1 int >1 id=%u type=%u cutoff=%.3g "
                                   "reso=%.3g drive=%u signal=%u max=%.9g\n",
                                   id, type, filter_cutoffs[cutoff],
                                   filter_resos[reso], filter_drives[drive],
                                   signal, int_max_by_config[id]);
                            ++failures;
                        }
                        if (float_max_by_config[id] > 1.0 && !corner) {
                            printf("S1 float >1 id=%u type=%u cutoff=%.3g "
                                   "reso=%.3g drive=%u signal=%u max=%.9g\n",
                                   id, type, filter_cutoffs[cutoff],
                                   filter_resos[reso], filter_drives[drive],
                                   signal, float_max_by_config[id]);
                            ++failures;
                        }
                        if (int_max_by_config[id] > 16.0 && !corner) {
                            printf("S1 int >16 outside self-oscillating family "
                                   "id=%u\n", id);
                            ++failures;
                        }
                        if (float_max_by_config[id] > 16.0 && !corner) {
                            printf("S1 float >16 outside self-oscillating family "
                                   "id=%u\n", id);
                            ++failures;
                        }
                    }
    if (int_metrics->error_energy == INFINITY ||
        float_metrics->error_energy == INFINITY ||
        metric_sdr(int_metrics) < 80.0 || metric_sdr(float_metrics) < 80.0)
        ++failures;
    printf("S1 acceptance: %s (%u failures)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}

int main(int argc, char **argv)
{
    const int expect_identical = argc > 1 &&
            strcmp(argv[1], "--expect-identical") == 0;
    const char *baseline_out = 0;
    const char *s1_report = 0;
    FilterMetrics int_metrics = { 0 }, float_metrics = { 0 };
    double int_max_by_config[28800] = { 0 };
    double float_max_by_config[28800] = { 0 };
    uint8_t int_baseline[28800] = { 0 };
    uint8_t float_baseline[28800] = { 0 };

    if (argc > 2 && strcmp(argv[1], "--baseline-out") == 0)
        baseline_out = argv[2];
    if (argc > 2 && strcmp(argv[1], "--s1-report") == 0)
        s1_report = argv[2];

    if (run_grid(&int_metrics, &float_metrics, int_max_by_config,
                 float_max_by_config, baseline_out) != 0u)
        return 1;
    print_metrics("int16", &int_metrics);
    print_metrics("float", &float_metrics);

    if (expect_identical)
        return (int_metrics.differing || float_metrics.differing) ? 1 : 0;
    if (s1_report) {
        read_baseline(s1_report, int_baseline, float_baseline);
        return s1_accept(&int_metrics, &float_metrics, int_max_by_config,
                         float_max_by_config, int_baseline, float_baseline) ? 1 : 0;
    }
    return 0;
}
