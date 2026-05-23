# SPECS_03 — FFT Pipeline

## 0. Contexto

El pipeline FFT ya no es el centro del sistema.
Es la **puerta de entrada** que produce la materia prima para los niveles ontológicos:

- N1 (bins) → raw spectrum → residual
- N2 (peaks) → energy maxima → candidates for tracking
- N3 (partials) → tracking → additive bank
- Transients → onset detection → tratamiento especial

El pipeline FFT es el primer eslabón de una cadena que termina en la resíntesis híbrida (SPECS_07).
No existe aislado: sus salidas alimentan directamente el partial tracking y la componente residual.

---

## 1. STFT

### Parámetros

| Parámetro | Default | Mínimo | Máximo | Impacto |
|-----------|---------|--------|--------|---------|
| Window size (N) | 2048 | 512 | 8192 | Resolución espectral vs temporal |
| Hop size (H) | N/4 | N/16 | N/2 | Overlap, densidad temporal |
| Window type | Hann | — | — | Smearing espectral |
| Zero padding | 2x | 1x | 4x | Interpolación espectral |
| FFT engine | vDSP (Apple) / JUCE DSP | — | — | Performance |

### Relaciones

- Δf = sampleRate / N
- Δt = H / sampleRate
- Latencia pipeline = H + N/2
- FFT engine: vDSP en macOS, JUCE dsp::FFT como fallback multiplataforma.

### Ejemplos

| N | H | SampleRate | Δf | Δt | Latencia |
|---|----|------------|----|-----|----------|
| 512 | 128 | 48kHz | 93.75 Hz | 2.67 ms | 5.33 ms |
| 1024 | 256 | 48kHz | 46.88 Hz | 5.33 ms | 10.67 ms |
| 2048 | 512 | 48kHz | 23.44 Hz | 10.67 ms | 21.33 ms |
| 4096 | 1024 | 48kHz | 11.72 Hz | 21.33 ms | 42.67 ms |

---

## 2. Window functions

### Ventana primaria: Hann

```
w[n] = 0.5 * (1 - cos(2πn/N)), 0 ≤ n ≤ N
```

Overlap-add perfecto con H = N/4.

### Ventanas alternativas

| Tipo | Uso |
|------|-----|
| Hann | Default — general purpose |
| Blackman-Harris | Análisis de alta calidad, máximos side lobes |
| Gaussian | Transiciones suaves para granularidad espectral |
| Kaiser | β ajustable, experimental |

---

## 3. Pipeline detallado (por dominio)

### 3.1 Pipeline común (cada frame, H samples)

```
1. Input buffer
   └─ read H new samples from ring buffer

2. Frame assembly
   └─ concatenar con N-H samples anteriores → frame de N

3. Window
   └─ multiplicar frame × window function

4. FFT forward
   └─ real → complex
   └─ obtener magnitud y fase por bin

5. Noise floor estimation
   └─ adaptive noise floor por bin
   └─ noise_floor[t] = α * noise_floor[t-1] + (1-α) * magnitude[t]
```

### 3.2 Branch tonal (N2 → N3)

```
6a. Peak detection
    └─ local maxima: |X[k]| > |X[k±1]|
    └─ threshold: |X[k]| > noise_floor * threshold_factor + min_energy

7a. Partial tracking
    └─ match peaks to existing partials (nearest neighbor)
    └─ birth: unmatched peak with sufficient energy
    └─ continuation: |freq - last_freq| < max_deviation
    └─ death: unmatched for N hold_frames

8a. Partial update
    └─ update frequency, amplitude, phase (per SPECS_08)
    └─ update coherence, stability, harmonic_affinity
    └─ update history ring buffer
```

### 3.3 Branch residual (N1)

```
6b. Tonal subtraction
    └─ reconstruct tonal spectrum from partials
    └─ subtract from original spectrum
    └─ residual = original - tonal

7b. Residual phase manipulation
    └─ random phase (noise) or diffused phase (fog)
    └─ según phase_mode y diffusion_amount (SPECS_08)

8b. Residual iFFT
    └─ inverse FFT
    └─ overlap-add → output frame
```

### 3.4 Branch transient

```
6c. Onset detection
    └─ spectral flux: sum(|X[k]| - |X_prev[k]|) over high bins
    └─ threshold crossing → transient detected

7c. Transient handling
    └─ según transient_mode (SPECS_08):
    └─ protect: mantener fase original, fade to processed
    └─ diffuse: aplicar diffusion completa
    └─ trigger: gatillar eventos (particle burst, N4)
    └─ kill: suprimir transiente
```

---

## 4. Partial tracking

### 4.1 Algoritmo (MVP)

Nearest-peak matching:

```
for each peak p at time t:
    nearest = argmin(|p.freq - partial.freq|) over active partials
    if |p.freq - nearest.freq| < max_freq_deviation:
        update(nearest, p)
    else:
        if p.energy > birth_threshold:
            birth(p)

for each partial at time t-1 not matched:
    hold_counter++
    if hold_counter > max_hold_frames:
        begin_death_fade(partial)
```

### 4.2 Criterios

```
max_freq_deviation = 0.5 * Δf (media resolución de bin) — ajustable
max_hold_frames = 3 — cuántos frames esperar antes de declarar muerte
birth_threshold = noise_floor * threshold_factor + min_energy
```

### 4.3 Coherence metrics

Por partial (calculado en tracking):

```cpp
float phase_coherence = 1 - |unwrap(Δφ_measured - Δφ_predicted)| / π;
float freq_stability = 1 - |freq_deviation| / max_freq_deviation;
float harmonic_affinity = compute_harmonic_affinity(partial, fundamental_candidates);

partial.coherence = phase_coherence;
partial.stability = freq_stability;
partial.harmonic_affinity = harmonic_affinity;
partial.temperature = 1 - phase_coherence; // para física (SPECS_05)
```

### 4.4 Smear constraints

El partial tracking debe mantener:

```
smear_ratio = diffused_partials / total_partials < smear_budget

smear_budget por modo:
  - modo identidad: < 0.3
  - modo textura: < 0.7
  - modo caos: sin límite
```

Un partial se considera "diffused" si: `partial.coherence < 0.3`.

---

## 5. Transient detection

### 5.1 Spectral flux

```
flux[t] = sum(|X_high[k][t]| - |X_high[k][t-1]|)
onset = flux[t] > flux[t-1] * onset_threshold (default: 3x)
```

High bins: k correspondientes a > 2kHz (donde viven la mayoría de transientes).

### 5.2 Acción por modo

| Modo | Acción en FFT pipeline |
|------|------------------------|
| Protect | Frame t: mantener fase original. Frame t+1: fade a fase procesada. |
| Diffuse | Frame t: aplicar diffusion máxima al residual. No reset. |
| Trigger | Frame t: forzar birth masivo de partículas N2 en banda alta. |
| Kill | Frame t: suprimir energía en banda alta (gate). |

---

## 6. Memory layout

### Buffers DSP worker

```
Input frame:     float[N]           (real)
Window:          float[N]           (pre-calculada)
FFT out:         float[N]           (complex interleaved: re, im, re, im...)
Magnitude:       float[N/2]
Phase:           float[N/2]
Previous phase:  float[N/2]
Phase vocoder:   float[N/2]         (φ_inst por bin)
Noise floor:     float[N/2]
Peaks:           Peak[MAX_PEAKS]    (struct: freq, amp, phase, bin_index)
Partials:        Partial[MAX_PARTIALS]  (struct completo)
Residual spec:   float[N/2]         (magnitud residual)
```

Todos pre-allocados en `prepareToPlay()`. Sin malloc en tiempo real.

### Optimizaciones

- Windows pre-calculadas.
- Buffers alineados a 16/32 bytes (vDSP requiere alignment).
- Pool de buffers para snapshots (2 buffers, swap atómico).

---

## 7. Noise floor and thresholding

### Adaptive noise floor

```text
noise_floor[k][t] = α * noise_floor[k][t-1] + (1-α) * magnitude[k][t]
α = 0.9 (suave) a 0.99 (muy suave)
Estimado en bins sin contenido armónico (determinado por tracking).
```

### Thresholds

```text
particle_threshold[k] = noise_floor[k] * threshold_factor + min_energy
threshold_factor: 1.5-5.0 (controlable por usuario)
min_energy: -80dB (evita falsos positivos en silencio)
```

### Dynamic adjustment

Si el sistema está cerca del límite de partials:
- threshold_factor sube (menos births, más muertes).
- Si hay pocos partials: threshold_factor baja.

---

## 8. Fases de implementación

### Fase 1 (MVP acústico)

- STFT básico con Hann + overlap-add.
- Peak detection simple.
- Partial tracking nearest-peak.
- Sin residual (solo tonal).
- Sin transientes.
- Phase vocoder básico (SPECS_08 Fase 1).

### Fase 2

- Tonal subtraction → residual.
- Residual iFFT con random phase.
- Onset detection + transient protect.
- Phase diffusion.

### Fase 3

- Harmonic affinity (múltiples fundamentales candidatos).
- Smear constraints.
- Spectral fog (diffusion por banda).

### Fase 4

- Transient kill/trigger modes.
- Phase energy transfer entre partials.
- Optimización SIMD.

---

## 9. Testing

### Pruebas offline

| Prueba | Qué verifica |
|--------|-------------|
| Sine sweep | Resíntesis reproduce sinusoide fielmente |
| Impulse | Respuesta transiente sin artefactos |
| Silence | Sin ruido de piso ni artefactos |
| White noise | Residual capture complete, sin tonales fantasma |
| Piano note | Partial tracking captura armónicos |
| Voice | Formantes preservados en modo balanceado |
| Drum loop | Transientes detectados y preservados |

### Métricas

- **Delay**: latencia end-to-end medida.
- **SNR**: relación señal/ruido de resíntesis vs dry (modo identidad).
- **Partial hit rate**: % de bins significativos capturados como partials.
- **Partial stability**: % de partials que sobreviven > 10 frames.
- **Smear ratio**: partials con coherence < 0.3 / total partials.
- **CPU**: uso por etapa del pipeline.

---

## 10. Relación con otros SPECS

| SPECS | Conexión |
|-------|---------|
| 07 — Resynthesis | El pipeline FFT alimenta tonal y residual; la resíntesis consume partials y espectro residual |
| 04 — Particle model | El tracking implementa el lifecycle N2→N3 del modelo ontológico |
| 08 — Phase management | El phase vocoder y coherence metrics se calculan aquí |
| 05 — Particle physics | La física modifica partials entre frames DSP; el tracking debe reconciliar estos cambios con los nuevos peaks |
