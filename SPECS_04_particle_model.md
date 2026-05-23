# SPECS_04 — Particle Model

## 0. Premisa

La resíntesis (SPECS_07) definió el destino acústico de las partículas.
Este spec define qué son, cómo nacen, cómo viven y cómo mueren.

El modelo no es único: es una jerarquía ontológica de 4 niveles.
Cada nivel emerge del anterior. Ninguno lo reemplaza.

---

## 1. Niveles ontológicos

### N1 — Bin particle

| Atributo | Valor |
|----------|-------|
| Origen | Un bin FFT individual |
| Vida | 1 frame (efímera) |
| Costo | Mínimo |
| Cantidad | N/2 por frame |
| Estado | Magnitud, fase, frecuencia |
| Resíntesis | Contribuye al residual FFT |
| Persistencia | Ninguna |
| Propósito | Base material del espectro |

```
bin[t] → particle
particle → output frame
particle dies
```

N1 es el nivel "raw". No hay identidad, no hay memoria.
Es el agua molecular del sistema.

---

### N2 — Peak particle

| Atributo | Valor |
|----------|-------|
| Origen | Peak detection sobre bins |
| Vida | Múltiples frames (inestable) |
| Costo | Bajo |
| Cantidad | 10-100 |
| Estado | Frecuencia, amplitud, fase, energía |
| Resíntesis | Oscilador simple o contribución parcial |
| Persistencia | Frame a frame si hay continuidad |
| Propósito | Energía local estable |

```
|X[k]| > threshold
AND
|X[k]| > |X[k±1]|
→ peak particle
```

N2 es un bin que sobrevive porque tiene energía significativa.
Es inestable pero ya no es ruido de fondo.

---

### N3 — Partial particle

| Atributo | Valor |
|----------|-------|
| Origen | Partial tracking sobre peaks |
| Vida | Segundos (estable, musical) |
| Costo | Medio |
| Cantidad | 16-256 |
| Estado | freq, amp, phase, age, coherence, stability, harmonic_affinity, history |
| Resíntesis | Additive oscillator bank (core tonal) |
| Persistencia | Birth → continuation → death |
| Propósito | Identidad musical tonal |

N3 es la partícula con identidad.
Tiene:

- **historial**: hacia dónde se movió, cómo cambió su energía.
- **coherencia**: qué tan estable es su fase.
- **estabilidad**: qué tan predecible es su frecuencia.
- **afinidad armónica**: qué tan cerca está de la serie armónica parcial.

Reglas de continuación (desde SPECS_07):

```
|freq(t) - freq(t-1)| < max_freq_deviation
AND |amp(t) - amp(t-1)| < max_amp_deviation
AND partial.age < max_age
```

Reglas de muerte:

- Energy < death_threshold → fade out.
- No peak match por N frames → hold + fade.
- Age > max_partial_age → release.
- User kill → click-protected silence.
- Collision/absorption (futuro) → transferencia de energía.

---

### N4 — Organism / Cluster

| Atributo | Valor |
|----------|-------|
| Origen | Grupos de partials con relaciones |
| Vida | Múltiples segundos o indefinida |
| Costo | Alto |
| Cantidad | 1-16 |
| Estado | Partículas miembro, centroide, relaciones, reglas internas |
| Resíntesis | Comportamiento colectivo |
| Persistencia | Emergente, depende de miembros |
| Propósito | Comportamiento espectral de alto nivel |

N4 es un conjunto de partículas N3 que:

- comparten afinidad armónica,
- mantienen relaciones de fase coherentes,
- se mueven como grupo,
- nacen y mueren juntas (o se dividen),
- tienen comportamiento emergente no programado explícitamente.

N4 NO se implementa en MVP.
Se diseña ahora para que N1-N3 no impidan N4 después.

---

## 2. Relación entre niveles

Las capas superiores emergen de las inferiores, no las reemplazan.

```
N4 Organism/Cluster
    │
    ├── emerge de ──→ N3 Partial particles
    │                     │
    │                     ├── emerge de ──→ N2 Peak particles
    │                     │                     │
    │                     │                     ├── emerge de ──→ N1 Bin particles
    │                     │                     │
    │                     │                     └── raw material
    │                     │
    │                     └── partial tracking
    │
    └── reglas de grupo
```

### Implicaciones arquitectónicas

- N1 siempre existe (es la FFT).
- N2 se calcula sobre N1.
- N3 se calcula sobre N2.
- N4 se calcula sobre N3.

Cada nivel es un filtro que extrae orden del nivel inferior.
No hay skipping: no se puede tener N3 sin N2 ni N4 sin N3.

### Múltiples niveles simultáneos

En un frame dado, coexisten:

- Cientos de N1 (bins).
- Decenas de N2 (peaks).
- Decenas de N3 (partials).
- 0-N de N4 (clusters).

Cada nivel se actualiza a su propia frecuencia:

| Nivel | Frecuencia de update |
|-------|---------------------|
| N1 | Cada frame FFT (H samples) |
| N2 | Cada frame FFT |
| N3 | Cada 1-4 frames (según estabilidad) |
| N4 | Cada 4-16 frames |

Esto permite optimización progresiva.

---

## 3. Lifecycle

### Birth

```
N1 birth: automático (cada bin)
N2 birth: peak detection
N3 birth: unmatched peak → new partial (si energía suficiente)
N4 birth: cluster de N3 con relaciones → new organism
```

Cada nivel tiene su propio umbral de birth:

```
N2_threshold = noise_floor * threshold_factor + min_energy
N3_threshold = N2_threshold * harmonic_bias (más permisivo si hay contenido tonal)
N4_threshold = f(N3 count, N3 coherence, N3 harmonic density)
```

### Life

Cada nivel tiene su propio reloj:

- N1: vive 1 frame.
- N2: vive hasta que el peak desaparece.
- N3: vive hasta que incumple reglas de continuación o alcanza max_age.
- N4: vive mientras tenga miembros activos + coherencia interna.

### Death

| Nivel | Muerte | Tiempo de fade |
|-------|--------|---------------|
| N1 | Fin del frame | 0 (siguiente FFT) |
| N2 | Peak desaparece | 1-2 frames |
| N3 | No match + hold | 5-50ms |
| N4 | Pérdida de coherencia | 100-500ms |

Los tiempos de fade más largos en niveles superiores crean la sensación de "inercia espectral".

---

## 4. Estado de partícula (N3 — el núcleo)

### Atributos persisted

```cpp
struct SpectralParticle {
    // Identity
    uint32_t id;           // unique, persistent
    uint64_t birth_frame;  // frame de nacimiento

    // Spectral
    float frequency;       // Hz (lineal interno)
    float amplitude;       // 0-1
    float phase;           // 0-2π (unwrapped para continuidad)
    float energy;          // energy acumulada / trackeada

    // Temporal
    float age;             // frames desde birth
    float lifetime;        // frames de vida restante
    float stability;       // 0-1 (desviación de frecuencia reciente)
    float coherence;       // 0-1 (desviación de fase reciente)

    // Behavioral
    float harmonic_affinity; // 0-1 (qué tan armónico es)
    float mass;            // relacionada a energía + estabilidad
    float drift;           // deriva de frecuencia (natural o forzada)

    // Spatial (para física y visual)
    float spectral_pos;    // posición en el eje frecuencia (normalizado)
    float spatial_x, spatial_y; // posición 2D para visual (opcional)

    // Relations
    uint32_t parent_id;    // partial del que nació (si tracking)
    uint32_t cluster_id;   // organismo al que pertenece (N4)

    // History (ring buffer)
    float history_freq[16];    // últimas 16 frecuencias
    float history_amp[16];     // últimas 16 amplitudes
    uint8_t history_idx;       // índice actual en ring buffer
};
```

**Tamaño:** ~120 bytes por partícula.
**256 partículas:** ~30KB — trivial.

### Atributos derivados (no persisted)

```cpp
float harmonic_number = frequency / fundamental;  // n-ésimo armónico
float inharmonicity = abs(frequency - n * fundamental) / (n * fundamental);
float spectral_centroid = energy_weighted_mean(frequency);
float entropy = -sum(p * log(p)) over energy distribution;
```

---

## 5. Partial drift

El drift es la deriva controlada de frecuencia de una partícula N3.

### Restricciones perceptuales

El drift no debe ser uniforme.
Debe ser proporcional a:

| Factor | Efecto | Drift resultante |
|--------|--------|-----------------|
| Baja energía | Más deriva | Alto drift |
| Alta estabilidad | Menos deriva | Bajo drift |
| Baja harmonic_affinity | Más deriva | Alto drift |
| Alta coherencia | Menos deriva | Muy bajo drift |
| Edad avanzada | Más deriva | Drift creciente |

### Modelo

```
drift_amount = base_drift
             * (1 - stability) * 0.4
             * (1 - energy) * 0.3
             * (1 - harmonic_affinity) * 0.2
             * (age / max_age) * 0.1

frequency += drift_amount * random(-1, 1) * sampleRate / N
```

Esto mantiene partículas estables y armónicas quietas,
mientras permite que las ruidosas e inestables deriven.

---

## 6. Harmonic affinity

### Cálculo

```python
fundamental = estimate_fundamental(partials)  # Mínimo común múltiplo o correlación

for each partial:
    expected_freq = fundamental * round(partial.freq / fundamental)
    deviation = abs(partial.freq - expected_freq) / expected_freq
    harmonic_affinity = 1 - min(deviation, 1)
```

### Efectos

- **Alta affinity** → partícula se "ancla" al armónico más cercano (harmonic locking).
- **Baja affinity** → partícula deriva libremente (inharmonic / noise).
- **Affinity media** → comportamiento híbrido, tira hacia armónico pero no se fija.

### Harmonic locking (controlable)

Cuando `harmonic_lock = ON`:
- Partial con affinity > umbral se fija a la frecuencia armónica exacta.
- Esto preserva musicalidad incluso con drift activo.

---

## 7. Coherencia de partícula

### Phase coherence

Mide qué tan estable es la fase entre frames:

```
phase_coherence = 1 - |unwrap(phase(t) - phase(t-1)) - expected_phase| / π
```

### Spectral coherence

Mide qué tan estable es el contenido espectral local:

```
spectral_coherence = correlation(magnitude_spectrum[t], magnitude_spectrum[t-1])
```

### Uso

- Partículas con alta coherencia → resíntesis fiel.
- Partículas con baja coherencia → candidatas a difusión o muerte.
- La coherencia global del sistema es un macro-parámetro de control artístico.

---

## 8. Persistencia y memoria

### Memoria de corto plazo

Cada partícula N3 mantiene un historial (ring buffer de 16 frames).

Uso:

- Estimar tendencia de frecuencia (¿sube? ¿baja? ¿estable?).
- Suavizar amplitud (evitar pops).
- Detectar inestabilidad.

### Memoria de largo plazo (post-MVP)

- Banco de "espectros recordados": snapshots del sistema en momentos T.
- Las partículas pueden "recordar" configuraciones previas y tender hacia ellas.
- Útil para performance: construir y destruir texturas.

---

## 9. Relación con resíntesis (SPECS_07)

| Nivel | Tipo | Destino en resíntesis |
|-------|------|----------------------|
| N1 | Bin | Residual FFT/iFFT (noise field) |
| N2 | Peak | Residual o candidato a partial |
| N3 | Partial | Additive oscillator bank (tonal) |
| N4 | Cluster | Comportamiento colectivo (futuro) |

La conexión es directa:
- N1 + N2 alimentan el residual.
- N3 alimenta el tonal.
- N4 modula ambos.

---

## 10. Relación con visualización

| Nivel | Visualización |
|-------|---------------|
| N1 | Malla/espectrograma de fondo |
| N2 | Puntos pequeños, efímeros |
| N3 | Partículas con estela, color por harmonic_affinity |
| N4 | Constelaciones, clusters con conectividad |

La visualización debe reflejar el nivel ontológico.
No todo se renderiza igual.

---

## 11. Performance targets

| Nivel | Cantidad típica | Costo relativo | Frecuencia de update |
|-------|----------------|----------------|---------------------|
| N1 | 2048 | 0.1x | Cada frame |
| N2 | 64 | 1x | Cada frame |
| N3 | 64-256 | 10x | Cada 1-4 frames |
| N4 | 0-16 | 50x | Cada 4-16 frames |

Estrategia: el usuario controla `max_particles` que afecta principalmente N3.
N1 y N2 son fijos. N4 se activa según recursos.

---

## 12. Implementación por fases

### MVP (Fase 1)

- Solo N1 + N2.
- N3 básico (partial tracking nearest-peak).
- Sin N4.
- 64 partials máximo.

### Fase 2

- N3 completo con drift + harmonic affinity.
- N4 experimental.
- 128 partials.

### Fase 3

- N4 estable.
- Memoria de largo plazo.
- 256 partials.

### Fase 4

- Niveles superiores con comportamiento emergente.
- Optimización.
- Límites según CPU target.

---

## 13. Preguntas abiertas

- ¿Deben los niveles N1-N4 ser acumulativos o excluyentes (solo un nivel activo)?
- ¿N4 debe tener su propia física o heredar la de N3?
- ¿Cómo se serializa el estado de 4 niveles?
- ¿El usuario debe ver los niveles o solo el resultado sonoro?
- ¿N3 debe tener bandwidth (no ser senos puros) desde el MVP?
