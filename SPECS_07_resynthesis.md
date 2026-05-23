# SPECS_07 — Resynthesis

## 0. Premisa

La resíntesis es el corazón acústico del proyecto.

Define:

- identidad sonora,
- viabilidad CPU,
- arquitectura threading,
- granularidad de partículas,
- phase strategy,
- latency budget,
- musicalidad.

Sin resíntesis funcional, no hay instrumento.

---

## 1. Modelo híbrido: SMS-like

SPI adopta un modelo split basado en Spectral Modeling Synthesis (Serra, 1989):

```
Input
  │
  ├──→ Componente tonal (determinista)
  │       partial tracking → oscillator bank → partículas coherentes
  │
  └──→ Componente residual (estocástico)
          FFT/iFFT → noise field → diffusion → spectral fog
```

### Justificación

| Estrategia | Ventaja | Desventaja |
|------------|---------|------------|
| Solo additive | Partículas autónomas, comportamiento emergente | CPU monstruosa, ruido ineficiente |
| Solo FFT/iFFT | Rápido, simple | Sin vida propia, sin emergencia real |
| Híbrido | Tonal → vida; Residual → textura | Más complejo, dos pipelines |

La hipótesis: lo tonal da identidad musical, lo residual da profundidad textural.

---

## 2. Componente tonal — Partial tracking + additive bank

### Pipeline

```
FFT bins
  │
  ├── Peak detection
  │     local maxima en magnitud
  │     umbral dinámico relativo al pico máximo
  │
  ├── Partial tracking
  │     asignar peaks consecutivos a partials
  │     criterio: frecuencia + magnitud + continuidad
  │     birth / continuation / death
  │
  ├── Particle mapping
  │     partial → partícula espectral
  │     atributos: freq, amp, phase, age, coherence
  │
  └── Additive oscillator bank
        sinuos por partícula
        envolvente de amplitud por frame
        fase continua (phase unwrapping)
```

### Partial tracking

#### Criterio de continuación

```
|freq(t) - freq(t-1)| < max_freq_deviation
AND
|amp(t) - amp(t-1)| < max_amp_deviation
AND
partial.age < max_age
```

#### Birth

- Un peak no asociado a ningún partial existente.
- Umbral mínimo de energía.
- Zona de exclusión temporal (evitar duplicados).

#### Death

- Un partial no encuentra peak durante `max_hold_frames`.
- O su energía cae por debajo de `death_threshold`.
- O su edad supera `max_partial_age` (si aplica).

### Oscillator bank

Cada partícula tonal genera:

```
output[n] = amp * sin(2π * freq / sampleRate * n + phase)
```

- Fase interpolada entre frames.
- Envolvente linear o coseno para evitar clicks en birth/death.
- Anti-aliasing: si freq > sampleRate/4, silenciar (o usar sinteble).

### CPU budget

```
Costo ≈ N_particles * (sin + envelope + phase_update)
```

Estimación inicial:

| Partículas | Costo relativo | Viabilidad |
|-----------|---------------|------------|
| 64 | 1x | Tiempo real garantizado |
| 128 | 2x | Tiempo real probable |
| 256 | 4x | Límite superior sin optimizar |
| 512+ | 8x+ | Requiere SIMD / GPU / reducción |

Estrategia: el usuario controla `max_particles`. El sistema ajusta dinámicamente.

---

## 3. Componente residual — FFT/iFFT noise field

### Pipeline

```
Input
  │
  ├── Tonal subtraction
  │     reconstruir espectro tonal
  │     restar del espectro original
  │     → residual spectrum
  │
  ├── Residual FFT/iFFT
  │     mantener magnitud
  │     fase randomizada o difusa
  │     ventana + overlap-add
  │
  └── Spectral noise shaping
        envelope espectral del residual
        difusión controlada por partículas de ruido
```

### Tonal subtraction

Opción A (precisa):
- Reconstruir espectro de partials.
- Restar en dominio espectral.
- Residual = original - tonal.

Opción B (rápida, MVP):
- Notch filter bank en frecuencias de partials.
- Residual = lo que no capturan los partials.

Opción C (híbrida):
- Spectral masking: umbral adaptativo por bin.
- Bins cerca de partials → tonal.
- Resto → residual.

### Fase residual

- Fase randomizada por frame → textura noise-like.
- Fase difusa con coherencia parcial → spectral fog.
- Fase congelada → drone textural.

### CPU budget

- Similar a FFT forward+inverse estándar.
- Dominado por iFFT y overlap-add.
- Costo fijo independiente de partículas.

---

## 4. Modos de resíntesis

### Modo 1: Dry (passthrough)

- Sin procesamiento.
- Útil para comparación A/B.

### Modo 2: Tonal only

- Solo additive bank.
- Partículas coherentes, sin ruido.
- Textura cristalina, metálica, pura.

### Modo 3: Residual only

- Solo noise field.
- Partial suppression total.
- Viento, fricción, textura amorfa.

### Modo 4: Híbrido balanceado

- Tonal + residual con blend control.
- Default del instrumento.

### Modo 5: Híbrido expandido

- Tonal con comportamiento emergente.
- Residual con difusión espectral.
- Partículas de ruido con dinámica propia.

---

## 5. Identity retention

### Qué sobrevive del input

| Componente | Tonal | Residual | Controlable |
|-----------|-------|----------|-------------|
| Pitch | Sí (partial tracking) | No | ±12 semitonos |
| Formantes | Parcialmente (envelope espectral) | No | Sí (EQ por banda) |
| Transientes | Sí (onset detection + phase reset) | No | Attack/decay |
| Envelope | Sí (amp tracking) | Envelope del noise | Compression |
| Articulación | Sí (partial birth/death timing) | Densidad de ruido | Temporal stretch |
| Ruido | No (va a residual) | Sí | Color, densidad |

### Qué puede mutar

| Atributo | Tonal | Residual | Rango típico |
|----------|-------|----------|-------------|
| Fase | Coherente o difusa | Random o difusa | Lock → Scatter |
| Energía | Envelope por partial | Envelope global | 0-200% |
| Localización espectral | Partial drift | Band shift | ±50% del rango |
| Harmonicidad | Harmonic locking | N/A | Lock → Inharm |
| Densidad | N° de partials | N° de noise bands | Escalar |
| Coherencia | Phase coherence | Diffusion rate | 0-1 |

### Lo que NO debe perderse

- **Identidad tonal** en modo híbrido balanceado.
- **Timbre** reconocible del instrumento fuente.
- **Articulación** (ataques, silencios, gestos).
- **Dinámica** (piano → forte no debe colapsar a ruido uniforme).

---

## 6. Muerte de partículas

### Tonal death

| Causa | Comportamiento | Sensación |
|-------|---------------|-----------|
| Energy threshold | Fade out (0.5-5ms) | Natural |
| Age limit | Fade out + release | Ciclo de vida |
| No peak match | Hold frames + fade | Pérdida de tracking |
| User kill | Instantáneo con click protection | Gestual |
| Collision/absorption | Transferencia de energía | Ecológica |

### Residual death

- Frame-based: la partícula residual muere al final del frame.
- Con difusión: fade out controlado por diffusion time.
- Noise particles: mismas reglas que tonales (si implementadas).

### Tails

La capacidad de extender la vida de partículas más allá del input es un diferencial clave.

```
tail_time = f(energy, harmonicity, user_param)
```

- Partículas armónicas → tails largos.
- Partículas inarmónicas o ruidosas → tails cortos o none.
- User override: `tail_amount` global.

---

## 7. Coherencia vs Caos

### Eje central

```
Coherence ←————————————————————————→ Chaos
     │                                    │
Preservación                          Emergencia
Identidad                             Descubrimiento
Reproducibilidad                      Sorpresa
```

### Parámetros de control

| Parámetro | Coherence → | ← Chaos |
|-----------|-------------|---------|
| Phase mode | Locked | Scrambled |
| Partial drift | 0% | 200% |
| Harmonic lock | On | Off |
| Diffusion rate | 0 | 1 |
| Birth threshold | Alto | Bajo |
| Particle life | Corto | Largo |
| Noise blend | 0% | 100% |

### Estrategia

El instrumento debe permitir moverse en este eje en tiempo real.
No como preset, sino como gesto continuo.

---

## 8. Arquitectura threading

### Thread 1: Audio callback

Solo:

- Captura de input.
- Mezcla dry/wet.
- Salida.

### Thread 2: DSP worker

- FFT forward.
- Peak detection.
- Partial tracking.
- Particle generation/update.
- Tonal subtraction → residual.

### Thread 3: Resynthesis

- Additive oscillator bank (tonal).
- iFFT + overlap-add (residual).
- Mezcla tonal + residual → output buffer.

### Thread 4: Simulation (futuro)

- Physics.
- Ecosystem rules.
- Clustering.

### Comunicación

| De | A | Contenido | Método |
|----|---|-----------|--------|
| Audio callback | DSP worker | Input buffer | Ring buffer |
| DSP worker | Resynthesis | Particle state snapshot | Double buffer + atomic swap |
| Resynthesis | Audio callback | Output buffer | Ring buffer |
| DSP worker | Simulation | Particle births | Command queue |

### Snapshot inmutables

- DSP worker escribe particle state en buffer A.
- Resynthesis lee desde buffer B.
- Al terminar frame DSP, swap A/B vía atomic pointer.
- Resynthesis nunca lee estado a mitad de actualización.

---

## 9. Latency budget

### Desglose

| Etapa | Latencia | Thread |
|-------|----------|--------|
| Input buffer fill | H samples | Audio callback |
| FFT + analysis | H samples | DSP worker |
| Resynthesis (additive) | H/2 samples | Resynthesis |
| Resynthesis (iFFT) | H samples | Resynthesis |
| Output buffer | ~0 (push) | Audio callback |
| **Total** | **~3.5 × H** | — |

Con H = 256 @ 48kHz: ~5.3ms + ~2.6ms = ~18.6ms total → dentro del límite.

### Optimizaciones futuras

- Superponer DSP worker con resynthesis (pipeline paralelo).
- Usar GPU para additive bank (OpenCL/CUDA/Metal).
- Reducir hop size en modo bajo latencia.

---

## 10. Parámetros de resíntesis (MVP)

| Parámetro | Rango | Default | Target |
|-----------|-------|---------|--------|
| Resynth mix | 0-1 | 0.5 | Dry/wet global |
| Tonal/residual | 0-1 | 0.7 | Balance tonal vs noise |
| Max partials | 16-256 | 64 | Límite de partials activos |
| Partial life | 0.1-10s | 2.0s | Tiempo de vida máximo |
| Partial drift | 0-0.5 | 0.02 | Deriva de frecuencia |
| Harmonic lock | On/Off | On | Forzar serie armónica |
| Phase mode | Lock/Diffuse/Scatter/Random | Lock | Estrategia de fase tonal |
| Diffusion rate | 0-1 | 0.3 | Difusión del residual |
| Noise color | White/Pink/Brown/Input | Input | Color del noise residual |
| Tail amount | 0-1 | 0.5 | Extensión de tails |
| Birth threshold | 0-1 | 0.1 | Sensibilidad de detección |
| Death threshold | 0-1 | 0.01 | Piso de energía |

---

## 11. Estrategia de implementación

### Fase 1 (MVP acústico)

- Additive bank básico (sine osc, sin optimizar).
- Partial tracking simple (peak → nearest partial).
- Sin residual (solo tonal).
- Modo dry/tonal only.
- 64 partials máximo.

### Fase 2

- Tonal subtraction.
- Residual FFT/iFFT con fase random.
- Modo híbrido.
- Partial locking armónico.

### Fase 3

- Phase diffusion.
- Harmonic locking/unlocking.
- Partial drift.
- Noise particles.

### Fase 4

- Tails avanzados.
- Comportamiento emergente en partials.
- Optimización SIMD/GPU.

---

## 12. Testing perceptual

### Pruebas de identidad

- Sine → ¿la resíntesis suena a sine?
- Piano → ¿sigue sonando a piano?
- Voz → ¿se entiende la voz?
- Ruido → ¿no introduce tonales fantasmas?

### Pruebas de calidad

- **Click test**: birth/death de partials sin clicks.
- **Drift test**: deriva gradual sin artefactos.
- **Noise test**: residual sin parcialidad tonal.
- **CPU stress**: 256 partials sin dropouts.

### Métricas

- **SNR** de resíntesis vs dry (modo identity max).
- **Latencia** medida end-to-end.
- **CPU** por número de partials.
- **Tonal ratio**: energía tonal / energía total.

---

## 13. Preguntas abiertas

- ¿El additive bank debe ser stereo o permitir spatialización por partial?
- ¿Los partials deben tener ancho de banda (no ser senos puros)?
- ¿El residual debe tener su propio sistema de partículas?
- ¿Debe haber “partículas híbridas” (tonal + noise band)?
- ¿Cómo manejar polifonía real (múltiples notas simultáneas)?
- ¿Transientes: tratarlos como partials especiales o dejarlos en residual?
