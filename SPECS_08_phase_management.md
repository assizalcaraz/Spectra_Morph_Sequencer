# SPECS_08 — Phase Management

## 0. Premisa

La fase es el pegamento perceptual del sistema.

Decide:

- qué tan estable suena,
- qué tan "vivo",
- cuánto smear es tolerable,
- cuánto caos perceptual es usable,
- cómo los transientes reorganizan el sistema,
- cómo se sincronizan las partículas entre sí.

Sin phase management, SPI es un generador de ruido espectral.

---

## 1. Dominios de fase

El sistema opera sobre 3 dominios de fase con comportamiento distinto:

### 1.1 Tonal phase

| Atributo | Valor |
|----------|-------|
| Partículas | N3 (partials) |
| Naturaleza | Coherente, continua |
| Pérdida tolerable | Baja |
| Estrategia | Phase vocoder + locking |
| Resíntesis | Additive oscillator bank |

La fase tonal debe mantenerse coherente entre frames para preservar identidad musical.

### 1.2 Residual phase

| Atributo | Valor |
|----------|-------|
| Partículas | N1 (bins), N2 (peaks no-trackeados) |
| Naturaleza | Estocástica o difusa |
| Pérdida tolerable | Alta |
| Estrategia | Randomización controlada |
| Resíntesis | iFFT con fase manipulada |

La fase residual no necesita coherencia. Es textura.

### 1.3 Transient phase

| Atributo | Valor |
|----------|-------|
| Origen | Onsets, ataques percusivos |
| Naturaleza | Discontinua, reset |
| Pérdida tolerable | Media (el ataque debe sobrevivir) |
| Estrategia | Phase reset + protección |
| Resíntesis | Mezcla especial con ventana más corta |

Los transientes rompen la coherencia del sistema y lo reorganizan.

---

## 2. Phase vocoder (base tonal)

### 2.1 Algoritmo estándar

Para cada bin con tracking:

```
Δφ = φ(t) - φ(t-1) - ω₀ * H
φ_inst = φ(t-1) + ω₀ * H + princarg(Δφ)
```

donde:

- `φ(t)`: fase actual
- `φ(t-1)`: fase anterior
- `ω₀`: frecuencia del bin (2π * k / N)
- `H`: hop size
- `princarg`: envuelve al rango [-π, π]

### 2.2 Instantaneous frequency

La frecuencia instantánea del partial no es la frecuencia del bin,
sino la derivada de la fase:

```
ω_inst = ω₀ + princarg(Δφ) / H
freq_inst = ω_inst * sampleRate / (2π)
```

Esto permite tracking sub-bin (resolución mayor que Δf).

### 2.3 Limitaciones del phase vocoder en SPI

El phase vocoder estándar asume:

- bins independientes,
- evolución lenta,
- sin discontinuidades.

SPI viola estos supuestos porque:

- las partículas se mueven (drift, física),
- nacen y mueren,
- el caos es un parámetro artístico.

Por lo tanto, SPI necesita capas adicionales sobre el phase vocoder base.

---

## 3. Jerarquía de coherencia

La coherencia de fase existe en 4 niveles:

### 3.1 Bin-level coherence

```
phase_coherence[k] = 1 - |Δφ_predicted - Δφ_measured| / π
```

Mide qué tan predecible es la fase de cada bin.
Uso: los bins con baja coherencia no generan partials.

### 3.2 Partial-level coherence

```
partial_coherence = mean(phase_coherence[k] over partial's spectral footprint)
```

Mide la estabilidad temporal de un partial.
Uso: partials con baja coherencia → drift permitido, death más probable.

### 3.3 Cluster-level coherence

```
cluster_coherence = mean(partial_coherence over cluster members)
                 * harmonic_agreement(cluster)
```

Mide la consistencia interna de un cluster N4.
Uso: clusters coherentes → comportamiento colectivo estable.
Cluster incoherente → disolución.

### 3.4 Global coherence

```
global_coherence = weighted_mean(partial_coherence)
```

Macro-parámetro que refleja el estado general del sistema.
Uso:

- **Alta** → sonido estable, identidad preservada.
- **Media** → textura viva, micro-variaciones.
- **Baja** → caos controlado, disolución espectral.

---

## 4. Phase locking strategies

### 4.1 Absolute locking (default tonal)

Cada partial mantiene su propia fase desenvuelta independientemente.

```
φ_partial(t) = φ_partial(t-1) + ω_inst * H
```

Pros: máxima coherencia por partial.
Contras: las relaciones de fase entre partials pueden degradarse.

### 4.2 Relative locking (harmonic relations)

Los partials armónicamente relacionados mantienen fase relativa constante.

```
φ_partial_k(t) = φ_fundamental(t) * k + ε_k
```

donde `ε_k` es el desvío de fase natural del k-ésimo armónico.

Pros: preserva la forma de onda, suena más "acústico".
Contras: menos libertad, más complejo.

### 4.3 Pseudo-locking (para inharmonic content)

Los partials sin relación armónica mantienen coherencia individual pero sin relaciones entre sí.

```
φ_partial(t) = φ_partial(t-1) + ω_inst * H + noise * diffusion_amount
```

Pros: permite texturas vivas sin perder identidad.
Contras: requiere control cuidadoso del diffusion_amount.

### 4.4 Harmonic locking (desde SPECS_04)

Cuando `harmonic_lock = ON` y `harmonic_affinity > threshold`:

- La frecuencia del partial se fija al armónico exacto.
- La fase se ajusta para mantener relación con fundamental.

Esto es un locking agresivo que preserva musicalidad incluso con drift activo.

---

## 5. Controlled decoherence

La decoherencia es la pérdida gradual de coherencia de fase.
No es binaria: es un continuo performático.

### 5.1 Parámetros de decoherencia

| Parámetro | Rango | Efecto |
|-----------|-------|--------|
| Phase diffusion | 0-1 | Ruido añadido a la fase |
| Phase scatter | 0-1 | Dispersión entre partials |
| Phase drift | 0-1 | Deriva de fase gradual |
| Phase reset rate | 0-1 | Frecuencia de reseteo de fase |

### 5.2 Estrategias de decoherencia

#### Diffusion (suave)

```
φ_smoothed = φ_clean * (1 - d) + random(-π, π) * d
```

donde `d = diffusion_amount`.

Efecto: textura viva, ligero smear, sin pérdida de identidad.

#### Scatter (dispersión)

Aplica offsets de fase aleatorios entre partials vecinos:

```
φ_partial_k += scatter_amount * random(-π, π) * proximity_weight(k, neighbors)
```

Efecto: ensanchamiento espectral, sensación de "nube".

#### Drift (deriva)

Deriva gradual de la fase de cada partial:

```
φ_partial(t) += drift_speed * age * random_walk()
```

Efecto: el sonido se "desenfoca" lentamente.

#### Reset (transiente)

Fase forzada a 0 en onsets detectados:

```
if onset_detected:
    φ_partial = 0
```

Efecto: ataque limpio, reorganización del sistema.

### 5.3 Mapeo al eje Coherence ↔ Chaos (SPECS_07)

```
Coherence ←──────────────────────────────────────────────→ Chaos
     │                                                       │
Locking                                                   Reset
Relative lock                                             Scatter
Absolute lock                                             Diffusion
No diffusion                                              High diffusion
No drift                                                   High drift
Phase vocoder                                             Random phase
```

El usuario controla su posición en este eje como gesto continuo.

---

## 6. Phase diffusion (residual)

### 6.1 Random phase per frame

La estrategia más simple para el residual:

```
φ_residual[k][t] = random(-π, π)
```

Pros: genera textura noise-like estable.
Contras: puede sonar "duro" frame a frame.

### 6.2 Diffused phase with coherence

Versión más suave:

```
φ_residual[k][t] = φ_residual[k][t-1] * α + random(-π, π) * (1-α)
```

donde `α` controla la inercia de fase.

- α = 0 → random total (ruido blanco)
- α → 1 → fase congelada (drone espectral)

### 6.3 Spectral fog

Variante donde la difusión varía por banda de frecuencia:

```
φ_residual[k][t] = interpolate(
    frozen_phase_low,
    random_phase_high,
    k / (N/2)
)
```

Efecto: bajos estables, altos difusos → textura natural.

### 6.4 Phase granulation (post-MVP)

Aplicar granularidad temporal a la fase residual:

- Segmentos de fase congelada (100-500ms).
- Transiciones suaves entre segmentos.
- Cada segmento: fase constante con micro-variaciones.

Efecto: textura tipo "cinta magnética espectral".

---

## 7. Transient handling

Los transientes son el evento más disruptivo para la coherencia de fase.
SPI necesita detectarlos y tratarlos como casos especiales.

### 7.1 Detección

Algoritmo simple basado en energía:

```
onset_energy = sum(|X[k]|²) over high-freq bins
if onset_energy[t] > onset_energy[t-1] * onset_threshold:
    transient_detected = true
```

Post-MVP: detector más refinado (phase deviation, spectral flux).

### 7.2 Comportamiento en transiente

Cuando se detecta un transiente:

| Componente | Acción |
|------------|--------|
| Partículas N3 | Phase reset parcial o total |
| Partículas N2 | Birth masivo de peaks |
| Partículas N1 | Frame completo preservado |
| Residual | Congelamiento o reset según modo |
| Coherencia global | Drop temporal seguido de recovery |

### 7.3 Transient protection

Para preservar el ataque sin artefactos:

1. Detectar onset.
2. En el frame del onset: mantener fase del bin original (no procesada).
3. Fade out del frame "protegido" en 2-3ms.
4. Reanudar fase procesada desde ahí.

### 7.4 Modos de transiente

| Modo | Comportamiento |
|------|---------------|
| Protect | Preservar ataque original (percusión, piano) |
| Diffuse | Difundir el transiente (ambient, textura) |
| Trigger | Usar el transiente para gatillar eventos N4 |
| Kill | Suprimir transientes (drone, pads) |

Controlable por usuario en tiempo real.

---

## 8. Phase energy transfer (ecosistema)

Las partículas pueden intercambiar coherencia de fase.

### 8.1 Coherence donation

Una partícula N3 estable puede "donar" coherencia a vecinas inestables:

```
φ_weak(t) = φ_weak(t) * (1 - donation) + φ_strong(t) * donation
```

Efecto: clusters que se sincronizan espontáneamente.

### 8.2 Phase absorption

Una partícula inestable puede "absorber" inestabilidad de vecinas,
actuando como sumidero de caos:

```
chaos_absorber.phase += sum(neighbor_phase_deviation)
chaos_absorber.energy += sum(neighbor_decay)
```

Efecto: partículas que "limpian" el entorno espectral.
Potencialmente útil para diseño de N4.

### 8.3 Phase locking via proximity

Partículas cercanas en frecuencia tienden a sincronizar fase:

```
φ_i(t) += coupling_strength * (mean(φ_neighbors) - φ_i(t))
```

Similar al modelo de Kuramoto para osciladores acoplados.

Efecto: emergencia de sincronización sin programación explícita.
Comportamiento natural que el usuario puede potenciar o inhibir.

---

## 9. Spectral smear control

El smear espectral es la pérdida de definición frecuencial.
Causado principalmente por:

- fase incoherente entre frames,
- partial tracking inexacto,
- diffusion agresiva,
- ventanas cortas.

### 9.1 Smear budget

Porcentaje del espectro que puede estar "manchado":

```
smear_ratio = diffused_energy / total_energy
```

Target: smear_ratio < 0.3 para modo identidad.
smear_ratio > 0.7 para modo textura.

### 9.2 Anti-smear strategies

| Técnica | Costo | Efectividad |
|---------|-------|-------------|
| Ventana más larga | Bajo | Media |
| Phase locking | Bajo | Alta |
| Partial tracking estable | Medio | Muy alta |
| Onset phase reset | Bajo | Alta (transientes) |
| Harmonic locking | Bajo | Alta (tonales) |

### 9.3 Smear como recurso artístico

El smear no es siempre indeseable.
Controlado, puede ser textura:

```
smear_intent = user_param (0 = limpio, 1 = manchado)
diffusion_amount = smear_intent * smear_intensity(freq_band)
```

---

## 10. Latency and phase

La fase y la latencia están acopladas.

### 10.1 Phase latency

Cada etapa del pipeline introduce desplazamiento de fase:

| Etapa | Desplazamiento |
|-------|---------------|
| FFT window | N/2 samples (linear phase) |
| Analysis | negligible |
| Resynthesis | H/2 a H samples |
| iFFT window | N/2 samples |
| Overlap-add | 0 |

### 10.2 Phase delay compensation

```text
total_phase_delay = window_delay + resynthesis_delay
```

Compensar en partial tracking: ajustar φ(t) para que corresponda
al centro del frame de análisis, no al inicio.

### 10.3 Lookahead (post-MVP)

Para transientes: buffer de lookahead de ~5ms.
Permite detectar onsets antes de procesarlos.
Reduce artefactos de fase en ataques.

---

## 11. Parámetros de fase (MVP)

| Parámetro | Rango | Default | Dominio |
|-----------|-------|---------|---------|
| Phase mode | Lock/Diffuse/Scatter/Random | Lock | Global |
| Diffusion amount | 0-1 | 0.1 | Global |
| Scatter amount | 0-1 | 0.0 | Global |
| Drift speed | 0-1 | 0.0 | Global |
| Harmonic locking | On/Off | On | Tonal |
| Transient mode | Protect/Diffuse/Trigger/Kill | Protect | Transient |
| Smear intent | 0-1 | 0.2 | Global |
| Coherence donation | 0-1 | 0.0 | N3-N4 |
| Coupling strength | 0-1 | 0.0 | N3 (Kuramoto) |

---

## 12. Estrategia de implementación

### Fase 1 (MVP)

- Phase vocoder básico para N3.
- Random phase para residual.
- Absolute locking.
- Sin transient protection (más adelante).
- Sin coupling.

### Fase 2

- Relative locking (harmonic relations).
- Transient detection + protect mode.
- Phase diffusion controlada.

### Fase 3

- Controlled decoherence como macro-parámetro.
- Scatter + drift.
- Spectral fog para residual.

### Fase 4

- Phase energy transfer.
- Kuramoto coupling.
- Transient triggering para N4.
- Lookahead.

---

## 13. Relación con otros SPECS

| SPECS | Conexión |
|-------|---------|
| 07 — Resynthesis | La fase decide la calidad del tonal y residual |
| 04 — Particle model | Coherence es atributo de N3; phase locking afecta harmonic affinity |
| 03 — FFT pipeline | Phase vocoder es extensión directa del STFT |
| 05 — Particle physics | Kuramoto coupling como fuerza física entre partículas |
| 06 — Spectral ecology | Coherence donation/absorption como regla ecosistémica |

---

## 14. Preguntas abiertas

- ¿La coherencia debe ser un parámetro por-partícula o global?
- ¿El Kuramoto coupling debe ser automático o gatillado por cercanía?
- ¿Los transientes deben resetear solo fase o también frecuencia de partials?
- ¿Debe haber un modo "frozen phase" (drone infinito)?
- ¿La fase puede ser un parámetro automatizable por el usuario en tiempo real?
- ¿Cómo se comporta la fase cuando hay múltiples fundamentales (polifonía)?
