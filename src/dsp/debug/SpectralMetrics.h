#pragma once

#include "../fft/FFTProcessor.h"
#include "../../core/types/Snapshot.h"
#include <cmath>
#include <cstdint>

struct SpectralMetrics {
    uint32_t num_peaks    = 0;
    uint32_t num_partials = 0;
    float    f0           = 0.0f;
    float    flux         = 0.0f;
    float    transient    = 0.0f;
    float    snr_db       = -120.0f;
    float    output_rms   = 0.0f;

    static float compute_rms(const float* buf, uint32_t n) {
        if (n == 0) return 0.0f;
        float sum = 0.0f;
        for (uint32_t i = 0; i < n; ++i)
            sum += buf[i] * buf[i];
        return std::sqrt(sum / static_cast<float>(n));
    }

    static float compute_snr_db(float signal_rms, float noise_rms) {
        if (noise_rms < 1e-12f) return 120.0f;
        if (signal_rms < 1e-12f) return -120.0f;
        return 20.0f * std::log10(signal_rms / noise_rms);
    }

    void from_frame(const FFTProcessor& fft, const ParticleSnapshot* snap,
                    const float* output, uint32_t hop,
                    float input_rms, float f0_hz)
    {
        flux      = fft.spectral_flux();
        transient = fft.transient_strength();
        f0        = f0_hz;
        output_rms = compute_rms(output, hop);
        if (snap) {
            num_partials = snap->num_partials;
        }
        const float err = std::abs(output_rms - input_rms);
        snr_db = compute_snr_db(input_rms, err + 1e-9f);
    }
};
