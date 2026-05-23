# SPECS_10 — Scheduler & Degradation Strategy

## 0. Premisa

El sistema tiene autonomía emergente y múltiples subsistemas compitiendo por CPU.
Sin un scheduler explícito, el realtime colapsa en cuanto:

- hay muchos partials,
- la ecología se activa,
- flocking escala a O(P²),
- el additive bank se acerca al límite.

Este spec define **qué se sacrifica y en qué orden**,
con hysteresis para evitar oscilación.

---

## 1. Prioridades de subsistema

Cada subsistema tiene una prioridad de 1 (máxima) a 8 (mínima).
Cuando el CPU se acerca al límite, se degradan en orden inverso.

| Prioridad | Subsistema | Thread | Costo base | Degradación posible |
|-----------|-----------|--------|------------|---------------------|
| 1 | Audio callback | Audio | ~1% | Ninguna — nunca se degrada |
| 2 | Additive bank | Resynthesis | ~30% | Reducir partials activos |
| 3 | FFT + peak detection | DSP worker | ~10% | Aumentar H (hop size) |
| 4 | Partial tracking | DSP worker | ~15% | Tracking cada 2-4 frames |
| 5 | Residual iFFT | Resynthesis | ~10% | Reducir resolución residual |
| 6 | Physics (fuerzas) | Simulation | ~10% | Reducir frecuencia, desactivar pares |
| 7 | Flocking + pares | Simulation | ~20% | Reducir radio, spatial hash, skip frames |
| 8 | Ecology (nichos, homeostasis) | Simulation | ~5% | Reducir frecuencia, solo pruning |

---

## 2. CPU pressure detection

### 2.1 Métrica

```cpp
struct CPUMetrics {
    // Por frame DSP
    uint64_t frame_start_ns;       // al inicio del proceso
    uint64_t frame_end_ns;         // al final
    uint64_t deadline_ns;          // H / sampleRate * 1e9

    // Rolling average (EMA)
    float load_ema;                // 0-1, suavizado sobre 16 frames
    float peak_ema;                // peak sobre 64 frames

    // Estado
    enum Pressure { NOMINAL, WARMING, HIGH, CRITICAL } pressure;
    uint32_t frames_since_recovery;
};
```

### 2.2 Umbrales

| Estado | Carga (EMA) | Acción |
|--------|-------------|--------|
| NOMINAL | < 0.6 | Sin degradación |
| WARMING | 0.6-0.75 | Preparar degradación suave |
| HIGH | 0.75-0.9 | Degradación activa |
| CRITICAL | > 0.9 | Degradación agresiva |

### 2.3 Hysteresis

Para evitar oscilación entre estados:

```
WARMING → HIGH: cuando load_ema > 0.75 por más de 4 frames consecutivos
HIGH → WARMING: cuando load_ema < 0.65 por más de 8 frames consecutivos
HIGH → CRITICAL: cuando peak_ema > 0.9 por más de 2 frames
CRITICAL → HIGH: cuando load_ema < 0.7 por más de 16 frames consecutivos
```

Sin hysteresis, el sistema oscila entre degradar y restaurar.

---

## 3. Degradation actions

### 3.1 Por nivel de presión

| Presión | Acción |
|---------|--------|
| WARMING | Reducir frecuencia de partial tracking: cada 2 frames |
| WARMING | Reducir frecuencia de physics: cada 2 frames |
| HIGH | Reducir partial budget dinámico: 256 → 192 → 128 |
| HIGH | Reducir flocking radius: 3 → 2 → 1 octava |
| HIGH | Aumentar hop size: N/4 → N/3 (menos frames por segundo) |
| CRITICAL | Partial budget: 128 → 64 |
| CRITICAL | Desactivar flocking completamente |
| CRITICAL | Reducir frecuencia de ecology: cada 4 frames |
| CRITICAL | Aumentar death_threshold para acelerar pruning |

### 3.2 Tabla completa

```cpp
struct DegradationState {
    // Parámetros modificables
    uint32_t dynamic_max_partials;      // empieza en 256, baja hasta 64
    uint32_t tracking_interval_frames;   // empieza en 1, sube hasta 4
    uint32_t physics_interval_frames;    // empieza en 1, sube hasta 4
    uint32_t ecology_interval_frames;    // empieza en 2, sube hasta 8
    float flocking_radius;              // empieza en 3, baja hasta 0 (off)
    uint32_t hop_divisor;               // empieza en 4 (N/4), sube hasta 2 (N/2)
    bool flocking_enabled;
    bool ecology_enabled;

    // Meta
    PressureLevel current_pressure;
    uint32_t frames_in_current_state;
};
```

---

## 4. Frame scheduling

### 4.1 Ejecución por frame DSP

Cada frame DSP (cada H samples), el scheduler decide qué ejecuta:

```cpp
void Scheduler::tick() {
    frame_counter++;

    // Siempre se ejecuta
    fft_processor.process();        // FFT + peak detection + noise floor

    // Cadencia variable según pressure
    if (frame_counter % tracking_interval == 0) {
        partial_tracker.track();    // Partial tracking + coherence metrics
    }

    if (frame_counter % physics_interval == 0) {
        particle_engine.update();   // Physics + forces
    }

    if (ecology_enabled && frame_counter % ecology_interval == 0) {
        ecology_engine.tick();      // Homeostasis, pruning, niches
    }

    // Siempre se ejecuta (pero con menos partials si hay pressure)
    resynthesizer.render();         // Additive bank + residual

    // Comunicación
    snapshot_swap();
    visual_state_send();
}
```

### 4.2 Límite de tiempo por etapa

Cada etapa tiene un presupuesto de tiempo en nanosegundos:

```cpp
struct TimeBudget {
    // Presupuesto total por frame: H / sampleRate * 0.8 (80% de un core)
    uint64_t total_ns;

    // Reparto
    uint64_t fft_budget_ns;          // 10% del total
    uint64_t tracking_budget_ns;     // 20%
    uint64_t physics_budget_ns;      // 15%
    uint64_t additive_budget_ns;     // 35%
    uint64_t residual_budget_ns;     // 10%
    uint64_t ecology_budget_ns;      // 5%
    uint64_t overhead_budget_ns;     // 5%
};
```

Si una etapa excede su presupuesto, el scheduler salta la siguiente ejecución de esa etapa y acumúa el déficit. Si el déficit supera un umbral, sube la presión.

---

## 5. Snapshot cadence

| Snapshot | Productor | Consumidor | Frecuencia |
|----------|-----------|------------|------------|
| Particle state A → B | DSP worker | Resynthesis | Cada frame tracking |
| Particle state B → A | Simulation | Resynthesis | Cada frame physics |
| Visual state | DSP worker | Comm thread | Cada 2-4 frames (o según visual budget) |

### Orden de swap

```
1. DSP worker termina tracking → swap snapshot A
2. Simulation lee snapshot A, aplica física → escribe snapshot B
3. Simulation termina → swap snapshot B
4. Resynthesis lee snapshot B (o A, el más reciente)
```

Si la simulación no ha terminado cuando resynthesis necesita leer, usa el snapshot anterior.

---

## 6. Dynamic partial budget

El número máximo de partials activos se ajusta dinámicamente según la presión:

```cpp
uint32_t compute_dynamic_budget(PressureLevel pressure) {
    switch (pressure) {
        case NOMINAL:  return 256;
        case WARMING:  return 192;
        case HIGH:     return 128;
        case CRITICAL: return 64;
    }
}
```

### Política de reducción

Cuando el budget baja, no se mata cualquier partial. Se usa pruning ecológico:

```
kill_candidates = sort_by(partials, key = energy * coherence)
kill_count = num_active - new_budget
kill(kill_candidates[0..kill_count])
```

### Política de expansión

Cuando el budget sube, no se crean partials artificialmente.
Solo se permite que más partials del input sobrevivan (menos pruning).

---

## 7. Cooldown and recovery

Después de un período de alta presión, el sistema no debe restaurar todo inmediatamente.

### Cooldown

```cpp
struct CooldownState {
    uint32_t frames_in_nominal;     // contador desde que entró a NOMINAL
    uint32_t required_frames;       // 64 frames (~1.3s @ 48kHz, H=512)
    uint8_t recovery_stage;         // 0=contenido, 1=restaurando, 2=libre
};
```

- Solo después de 64 frames en NOMINAL se empieza a restaurar.
- Cada 16 frames se restaura un nivel de degradación.
- Si la presión sube durante la recuperación, se revierte inmediatamente.

### Recovery order

El orden de restauración es inverso al de degradación:

1. Ecology (reactivar)
2. Flocking (reactivar, radio pequeño → creciente)
3. Physics frequency (cada 2 → cada 1 frame)
4. Tracking frequency (cada 2 → cada 1 frame)
5. Hop size (N/3 → N/4)
6. Partial budget (128 → 192 → 256)

---

## 8. Frame skipping

Si el CPU está en CRITICAL, el scheduler puede **saltar frames completos** de simulación:

```cpp
void Scheduler::decide_skip() {
    if (pressure == CRITICAL && (frame_counter % 2 == 0)) {
        // Saltar este frame de simulación
        // No ejecutar physics ni ecology
        // Tracking se ejecuta a frecuencia reducida
        return;
    }
}
```

Esto reduce la carga a la mitad inmediatamente.
El sistema se vuelve menos响应ivo pero no produce dropouts.

---

## 9. Integración con visual engine

El visual engine tiene su propio presupuesto y degradación.

- Frecuencia de update: 30 fps objetivo.
- Si el CPU está HIGH: 15 fps.
- Si CRITICAL: 10 fps o pausar.

La comunicación visual se hace en un thread separado con prioridad baja.
Nunca bloquea al audio.

---

## 10. Parámetros expuestos al usuario

| Parámetro | Rango | Default | Efecto |
|-----------|-------|---------|--------|
| CPU target | 50-90% | 75% | Qué tan agresiva es la degradación |
| Quality mode | {live, eco, studio} | eco | Preset de degradación |
| Max partials | 16-256 | 256 | Límite superior manual |
| Force performance | {off, on} | off | Degradación máxima forzada |

---

## 11. Comportamiento en silencio (sin input)

Cuando no hay input:

- El sistema no necesita hacer FFT/tracking pesado.
- Puede mantener partials existentes con energy decay lento.
- O entrar en modo "sustain" (SPECS_06 — homeostasis).

```cpp
void Scheduler::detect_silence() {
    if (input_energy < silence_threshold for 64 frames) {
        // Reducir tracking frequency a cada 8 frames
        // Reducir physics a cada 8 frames
        // Mantener additive bank para tails
        // No desactivar nada — permitir decay natural
    }
}
```

---

## 12. Resumen: matriz degradación

| Subsistema | NOMINAL | WARMING | HIGH | CRITICAL |
|-----------|---------|---------|------|----------|
| Partial budget | 256 | 192 | 128 | 64 |
| Tracking interval | 1 | 2 | 2 | 4 |
| Physics interval | 1 | 2 | 4 | 4 |
| Ecology interval | 2 | 4 | 4 | 8 |
| Flocking radius | 3 oct | 2 oct | 1 oct | off |
| Hop divisor | 4 | 4 | 3 | 3 |
| Flocking enabled | sí | sí | sí | no |
| Ecology enabled | sí | sí | sí | solo pruning |
