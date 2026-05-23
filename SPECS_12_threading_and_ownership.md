# SPECS_12 — Threading & Ownership Model

## 0. Premisa

El sistema tiene 4 threads con restricciones radicalmente distintas.
Este spec define **exactamente** quién se comunica con quién, cómo, y qué está prohibido en cada contexto.

Sin esto, los primeros bugs de realtime aparecen en la semana 1 y no se detectan hasta el primer dropout en vivo.

---

## 1. Topología de threads

| Thread | Prioridad | Realtime | Responsabilidad | Frecuencia |
|--------|-----------|----------|----------------|------------|
| Audio | Máxima (OS) | Hard RT | Captura input, mezcla output, copy buffers | Cada block (64-256 samples) |
| DSP Worker | Alta | Soft RT | FFT, peak detection, partial tracking, noise floor, snapshot write | Cada 1-4 frames DSP |
| Simulation | Normal | No | Physics, flocking, ecology, homeostasis, pruning | Cada 2-8 frames DSP |
| UI | Normal | No | Rendering JUCE, parameter changes, OSC receive | 30-60 fps |

No hay thread "Visual/IPC" separado — el visual engine es un proceso aparte (openFrameworks) que recibe datos vía OSC/UDP desde el DSP Worker (best-effort, no crítico).

---

## 2. Comunicación entre threads

Cada flecha es un canal de comunicación.
Solo se permiten los canales definidos aquí.

```
Audio ──SPSC(input)──→ DSP Worker
DSP Worker ──snapshot(atomic*)──→ Simulation
Simulation ──snapshot(atomic*)──→ DSP Worker  (merge)
DSP Worker ──snapshot(atomic*)──→ Resynthesis  (código en DSP Worker thread)
DSP Worker ──SPSC(visual)──→ OSC out → Visual Engine
UI ──atomic cache──→ DSP Worker  (parameter changes)
DSP Worker ──SPSC(telemetry)──→ UI  (para display)
```

### Leyendas

| Canal | Método | Locks | Tamaño |
|-------|--------|-------|--------|
| `SPSC(input)` | Ring buffer lock-free (JUCE AbstractFifo) | No | 8192 floats |
| `snapshot(atomic*)` | Double buffer con atomic pointer swap | No | sizeof(ParticleSnapshot) |
| `SPSC(visual)` | Ring buffer lock-free | No | 64 slots de VisualState |
| `atomic cache` | std::atomic<float> por parámetro | No | 8 bytes x N_PARAMS |
| `SPSC(telemetry)` | Ring buffer lock-free | No | 32 slots de TelemetryFrame |

### Prohibiciones absolutas

```text
- Ningún mutex, spinlock, semaphore en audio thread o DSP worker.
- Ningún malloc/free/new/delete en threads realtime.
- Ninguna excepción que pueda propagarse a audio thread.
- Ninguna llamada a sistema (I/O, socket, file) en threads realtime.
- Ningún std::function (heap alloc).
- Ninguna STL container (heap alloc) — usar arrays estáticos.
- Ninguna espera activa (busy wait).
```

---

## 3. Snapshot ownership

### Estado canónico

El **DSP Worker** es el propietario del estado canónico de las partículas.

- DSP Worker escribe `Snapshot A`.
- Al terminar tracking, hace `atomic_swap(read_ptr, write_ptr)`.
- Simulation recibe `read_ptr` y escribe `Snapshot B` (copia).

### Flujo exacto

Por frame DSP:

```
1. DSP Worker:
   a. FFT + peak detection
   b. Partial tracking (birth, continuation, death)
   c. Escribe Snapshot A (canónico)
   d. atomic_swap(A, B)   → Simulation ahora lee A
   e. Encola visual state

2. Simulation (si le toca este frame):
   a. Lee Snapshot A (inmutable, no escribe en A)
   b. Aplica physics + ecology sobre COPIA local
   c. Escribe Snapshot B con resultados
   d. atomic_swap(B, A)   → DSP Worker ahora lee B

3. Resynthesis (corre inmediatamente después de DSP):
   a. Lee el snapshot más reciente (A o B)
   b. Descompone a SoA
   c. Additive bank + residual
   d. Escribe output ring buffer
```

### Reglas

- DSP Worker **nunca** lee Snapshot B mientras Simulation lo escribe.
- Simulation **nunca** escribe Snapshot A mientras DSP Worker lo escribe.
- Resynthesis **nunca** escribe snapshots. Solo lee.
- Si Simulation no ha terminado cuando Resynthesis necesita leer, usa el snapshot anterior.

### Merge policy

Simulation produce snapshot completo, no delta.
Razón:
- Los deltas requieren locking o CAS complicado.
- Una copia completa de 256 partials = 24 KB. En L1. Trivial.
- La atomic swap de un puntero es la operación más barata posible.

---

## 4. Parameter smoothing

### Cambios de parámetro

- UI escribe `std::atomic<float>` cache.
- DSP Worker lee al inicio de cada frame DSP.
- El cambio no es sample-accurate — es frame-accurate (cada H samples).

### Ramp

Para evitar transiciones abruptas en parámetros críticos:

```cpp
struct SmoothedParameter {
    std::atomic<float> target;   // escrito por UI thread
    float current;                // leído por DSP worker
    float ramp_speed;             // por frame DSP

    float next() {
        if (abs(current - target.load()) < 0.001f) {
            current = target.load();
        } else {
            current += (target.load() - current) * ramp_speed;
        }
        return current;
    }
};
```

- `ramp_speed` = 0.1 (suave) a 0.5 (rápido).
- Parámetros que usan ramp: `coherence_chaos`, `density`, `gravity`, `motion`, `tonal_residual`.
- Parámetros que NO usan ramp: `dry_wet` (cambio inmediato, mezcla en audio callback).

### Parámetros automáticos (scheduler)

El scheduler puede modificar parámetros internos (degradation) sin pasar por la UI.
Escribe directo al `atomic<T>` correspondiente.

---

## 5. Timing y deadlines

```cpp
struct ThreadTiming {
    // Audio
    uint32_t block_size;           // 64-256 samples
    float sample_rate;             // 44100-48000 Hz
    float block_duration_ns;       // block_size / sample_rate * 1e9

    // DSP Worker
    uint32_t hop_size;             // N / hop_divisor
    uint32_t frame_duration_ns;    // hop_size / sample_rate * 1e9
    uint32_t max_fft_time_ns;      // 0.1 * frame_duration
    uint32_t max_tracking_time_ns; // 0.3 * frame_duration
    uint32_t max_resynth_time_ns;  // 0.4 * frame_duration
    uint32_t max_overhead_ns;      // 0.1 * frame_duration

    // Simulation (más flexible)
    uint32_t max_simulation_time_ns; // 0.5 * frame_duration * sim_interval
};
```

Si DSP Worker excede su deadline, el scheduler degrada (SPECS_10).
Si Simulation excede su deadline, se salta el siguiente frame de simulación.

---

## 6. Startup y shutdown

### Startup sequence

```
1. prepareToPlay(sampleRate, blockSize):
   - Pre-allocar PartialPool (MAX_PARTIALS)
   - Pre-allocar FFT buffers (FFT_MAX_SIZE)
   - Pre-allocar ring buffers
   - Pre-allocar snapshots (2)
   - Crear ventanas FFT pre-calculadas
   - Resetear scheduler
   - Cargar preset inicial

2. Iniciar threads:
   - DSP Worker thread (creado en prepareToPlay, dormido hasta primer processBlock)
   - Simulation thread (creado en prepareToPlay, dormido)
   - UI thread (gestionado por JUCE)
```

### Shutdown sequence

```
1. stopPlaying() / destructor:
   - Signal a DSP Worker: stop flag
   - Signal a Simulation: stop flag
   - Audio callback: últimas iteraciones drenan buffers
   - join(DSP Worker)
   - join(Simulation)
   - Liberar memoria pre-allocada
```

### Error handling

Si un thread realtime encuentra una condición irrecuperable:

- Setear `error_flag` atómico.
- El scheduler detecta el flag en el siguiente frame.
- Degradación máxima (solo passthrough).
- Loggear error desde UI thread (no-realtime).

---

## 7. Visual engine communication

- DSP Worker escribe `VisualState` en ring buffer lock-free.
- Un thread de comunicación (prioridad baja) lee y envía vía OSC.
- Visual engine (proceso separado) recibe OSC y renderiza.

```cpp
struct VisualState {
    uint32_t frame_number;
    uint32_t num_partials;
    Partial partials_for_visual[MAX_PARTIALS];  // misma struct, pero solo lee freq/amp/pos
    float global_coherence;
    float total_energy;
    uint8_t macro_state;   // stable, bloom, etc.
    uint16_t fps;
};
```

Frecuencia de envío: target 30 fps. Si CPU pressure, bajar a 15 fps.
Nunca bloquea al DSP Worker.

---

## 8. Resumen de reglas

| Regla | Ámbito |
|-------|--------|
| No malloc en audio thread ni DSP worker | Realtime safety |
| No locks en audio thread ni DSP worker | Realtime safety |
| No I/O en threads realtime | Realtime safety |
| Toda comunicación es SPSC (single producer, single consumer) | Ownership |
| Snapshot swap via atomic pointer, no memcpy | Performance |
| Parameter changes via atomic cache, smoothed | Estabilidad |
| Simulation produce snapshot completo, no delta | Simplicidad |
| El estado canónico pertenece al DSP Worker | Ownership |
| Visual engine es best-effort, no crítico | Degradación |
