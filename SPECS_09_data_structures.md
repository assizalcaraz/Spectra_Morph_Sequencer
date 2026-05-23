# SPECS_09 — Data Structures & Memory Layout

## 0. Premisa

Este spec no agrega comportamiento. Cierra **cómo se representa físicamente en memoria** cada entidad del sistema, quién escribe qué, y cuáles son los límites duros.

Sin esto, la implementación produce:
- cache misses evitables,
- ownership races no detectadas,
- límites implícitos que explotan en runtime,
- structs que cambian cada semana.

---

## 1. Límites duros

| Constante | Valor | Justificación |
|-----------|-------|---------------|
| `MAX_PARTIALS` | 256 | CPU budget del additive bank (SPECS_07) |
| `MAX_PEAKS` | 512 | N/4 para N=2048, sobremuestreo seguro |
| `MAX_BIRTHS_PER_FRAME` | 32 | Evita explosion en transientes |
| `MAX_NEIGHBORS` | 16 | Flocking y atracción localizada |
| `MAX_HISTORY` | 16 | Ring buffer de historial por partial |
| `MAX_CLUSTERS` | 16 | N4 máximo en alpha |
| `MAX_ATTRACTORS` | 8 | Campos externos simultáneos |
| `MAX_THREADS` | 4 | Audio, DSP, resynthesis, simulation |
| `SNAPSHOT_COUNT` | 2 | Double buffering |
| `RING_BUFFER_SIZE` | 8192 | Samples de input/output |
| `FFT_MAX_SIZE` | 8192 | Máxima ventana FFT |
| `LOG_OCTAVES` | 10 | 20 Hz – 20 kHz en log-space |

Estos límites son **hard floors**. El sistema nunca excede estas constantes en tiempo real. Si se acerca, pruning o degradation entran antes.

---

## 2. Structs principales

### 2.1 Partial (N3)

```cpp
// Aligned to 16 bytes for SIMD
struct alignas(16) Partial {
    // Identity — lectura constante, escritura solo en tracking
    uint32_t id;                // 4 bytes
    uint32_t birth_frame;       // 4
    uint32_t lineage_id;        // 4 (post-MVP)
    uint32_t parent_id;         // 4

    // Spectral core — escritura por DSP worker + simulation
    float frequency;            // 4
    float amplitude;            // 4
    float phase;                // 4
    float energy;               // 4

    // Temporal — escritura por simulation
    float age;                  // 4
    float lifetime_remaining;   // 4
    float stability;            // 4
    float coherence;            // 4

    // Behavioral — escritura por simulation + ecology
    float harmonic_affinity;    // 4
    float mass;                 // 4
    float drift;                // 4
    float temperature;          // 4 (1 - coherence)

    // Spatial — escritura por simulation
    float spectral_pos;         // 4 (log-frequency normalized 0..10)
    float velocity;             // 4 (Δspectral_pos / frame)
    float spatial_x;            // 4 (visual, opcional)
    float spatial_y;            // 4 (visual, opcional)

    // Lifecycle — escritura por scheduler
    uint8_t state;              // 1 (alive, dying, dead, frozen)
    uint8_t niche;              // 1 (drone, scavenger, etc)
    uint8_t hold_counter;       // 1 (frames sin match)
    uint8_t _pad;               // 1

    // Total: 88 bytes (con padding a 96 para alineación de cache line)
    uint8_t _pad2[8];
};
// static_assert(sizeof(Partial) == 96);
```

**Total para 256 partials**: 24 KB — cabe en L1 cache de cualquier CPU moderna.

### 2.2 Peak (N2)

```cpp
struct alignas(8) Peak {
    float frequency;        // 4
    float magnitude;        // 4
    float phase;            // 4
    uint16_t bin_index;     // 2
    uint8_t _pad[2];        // 2
};
// Total: 16 bytes
```

**Total para 512 peaks**: 8 KB.

### 2.3 Snapshot (particle state)

```cpp
struct alignas(64) ParticleSnapshot {
    uint32_t frame_number;              // 4
    uint32_t num_partials;              // 4
    float global_coherence;             // 4
    float total_energy;                 // 4
    Partial partials[MAX_PARTIALS];     // 96 * 256 = 24576
    // Total: ~24.6 KB — cabe holgadamente en L1/L2
};
```

Dos snapshots: **~50 KB total**. Trivial.

### 2.4 Cluster (N4, post-MVP)

```cpp
struct alignas(32) Cluster {
    uint32_t id;
    uint32_t member_ids[MAX_PARTIALS / 4];  // Bitmask de miembros
    uint32_t member_count;
    float centroid_freq;
    float centroid_energy;
    float internal_coherence;
    float harmonic_spread;
    uint8_t state;
    uint8_t _pad[7];
};
// Total: ~40 bytes + bitmap (64 entradas = 256 bits = 32 bytes) = ~72 bytes
```

### 2.5 External field (attractor / repeller / wind)

```cpp
struct alignas(16) ExternalField {
    float position;             // spectral_pos (log)
    float strength;             // -10..10 (negativo = repeller)
    float radius;               // en octavas
    uint8_t type;               // 0=attractor, 1=repeller, 2=wind
    uint8_t _pad[3];
};
// Total: 16 bytes
```

---

## 3. SoA vs AoS

| Sistema | Layout | Razón |
|---------|--------|-------|
| Partial tracking | AoS | Cada partial se accede como unidad; el tracking actualiza todos los campos |
| Additive bank | **SoA** | El banco de osciladores itera sobre frecuencia/amplitud/fase de TODOS los partials — cache miss masivo si es AoS |
| Peak detection | AoS | Se accede un peak a la vez |
| Physics | AoS | Las fuerzas se computan por partial |
| Snapshot | AoS | Se copia como bloque plano |

### Additive bank en SoA

```cpp
struct AdditiveBuffer {
    alignas(32) float freq[MAX_PARTIALS];     // 1024 bytes
    alignas(32) float amp[MAX_PARTIALS];      // 1024
    alignas(32) float phase[MAX_PARTIALS];    // 1024
    alignas(32) float env[MAX_PARTIALS];      // 1024 (envelope de birth/death)
    uint32_t active_mask[(MAX_PARTIALS + 31) / 32];  // 8 * 4 = 32 bytes
};
// Total: ~4 KB — L1 completa
```

El DSP worker escribe el snapshot en AoS. La resíntesis lo descompone a SoA al recibirlo.

---

## 4. Memory pools

### Pool de partials

Array contiguo de `MAX_PARTIALS` Partial. Los slots se marcan como libres/ocupados via bitmask.

```cpp
struct PartialPool {
    Partial partials[MAX_PARTIALS];         // 24 KB
    uint32_t free_mask[(MAX_PARTIALS + 31) / 32];  // 8 uint32 = 32 bytes
    uint32_t num_active;

    uint32_t allocate();      // first free slot
    void free(uint32_t id);   // mark slot as free
    bool is_alive(uint32_t id);
};
```

No hay malloc en tiempo real. Toda la memoria se pre-asigna en `prepareToPlay()`.

### Pool de peaks

Array contiguo de `MAX_PEAKS` Peak.
Se regenera completamente cada frame — no necesita alloc/free por slot.
Solo resetear contador.

---

## 5. Ownership table

| Field | Escritor(es) | Lector(es) |
|-------|-------------|------------|
| `Partial.id` | DSP worker (birth) | Simulation, Resynthesis |
| `Partial.frequency` | DSP worker + Simulation | Resynthesis |
| `Partial.amplitude` | DSP worker | Resynthesis |
| `Partial.phase` | DSP worker (phase vocoder) | Resynthesis |
| `Partial.energy` | DSP worker + Simulation | Ecology |
| `Partial.age` | Scheduler (cada frame) | Simulation, Ecology |
| `Partial.stability` | Scheduler (cada N frames) | Simulation |
| `Partial.coherence` | DSP worker (cada frame) | Resynthesis, Ecology |
| `Partial.harmonic_affinity` | DSP worker (partial tracking) | Simulation, Ecology |
| `Partial.mass` | Simulation | Physics |
| `Partial.drift` | Simulation | Physics |
| `Partial.temperature` | Simulation | Physics, Ecology |
| `Partial.spectral_pos` | Simulation | Resynthesis |
| `Partial.velocity` | Simulation | Physics |
| `Partial.state` | Scheduler + Simulation | Todos |
| `Partial.niche` | Ecology | Simulation, Visual |
| `Partial.hold_counter` | DSP worker (tracking) | Scheduler |
| `Partial.history[]` | DSP worker | Simulation, Visual |

**Regla de oro**: ningún campo es escrito por más de 2 threads.
Si ocurre, hay que agregar un snapshot intermedio o atomic.

---

## 6. Cache line strategy

### Partial layout per cache line (64 bytes)

Un Partial de 96 bytes cabe en 2 cache lines:

```
Cache line 0 (bytes 0-63):
  id, birth_frame, lineage_id, parent_id,      // 16
  frequency, amplitude, phase, energy,          // 16
  age, lifetime_remaining, stability, coherence, // 16
  harmonic_affinity, mass, drift, temperature   // 16

Cache line 1 (bytes 64-95):
  spectral_pos, velocity, spatial_x, spatial_y, // 16
  state, niche, hold_counter, pad,              // 4
  pad2[8]                                       // 8
  // + 12 bytes libres
```

**Implicación**: el additive bank solo necesita cache line 0 (freq, amp, phase).
La simulación usa ambas. La ecología usa principalmente cache line 0.

### False sharing prevention

- Snapshots en direcciones separadas por 64 bytes (`alignas(64)`).
- Contadores de scheduler en variables atómicas separadas.
- Cada thread escribe en regiones de memoria que ningún otro thread escribe en el mismo cache line.

---

## 7. Memory budget total

| Componente | Tamaño |
|------------|--------|
| Partial pool | 24 KB |
| Peak pool | 8 KB |
| Snapshot A + B | 50 KB |
| Additive SoA buffer | 4 KB |
| FFT buffers (N=2048, complex) | 16 KB |
| Ring buffer input | 32 KB |
| Ring buffer output | 16 KB |
| History arrays | 16 KB |
| Cluster pool (16) | ~1 KB |
| External fields (8) | ~0.1 KB |
| Misc (noise floor, prev phase, etc) | 16 KB |
| **Total** | **~180 KB** |

Cabe en L2 cache de cualquier CPU moderna.
No hay allocaciones dinámicas en tiempo real.

---

## 8. Serialización

### Preset format

```cpp
struct Preset {
    char magic[4];              // "SPI\0"
    uint32_t version;

    // Parameters
    float params[NUM_PARAMS];   // índice → valor

    // Ecology config
    float reproduction_threshold;
    float mutation_rate;
    float carrying_capacity_per_octave;
    float energy_floor;

    // Physics config
    float gravity;
    float attraction;
    float repulsion;
    float damping;
    float noise_amplitude;

    // Resynthesis config
    float resynth_mix;
    float tonal_residual_balance;
    uint8_t phase_mode;
    float diffusion_amount;
    float tail_amount;
    uint8_t transient_mode;

    // Macro
    float coherence_chaos;
    float spectral_centroid;
    float density;

    // External fields
    ExternalField fields[MAX_ATTRACTORS];
    uint8_t num_fields;

    // Checksum
    uint32_t crc32;
};
```

Serializado como binary blob, no XML. XML es para debugging.
Binary es para load/save rápido en el callback (si es necesario).

---

## 9. Preguntas cerradas

- **¿SoA o AoS?** → Ambos. AoS para tracking/physics, SoA para additive bank.
- **¿Malloc en tiempo real?** → Nunca. Todo pre-allocado.
- **¿Límite de partials?** → 256 hard. El usuario puede setear menos, nunca más.
- **¿Snapshots mutables?** → No. Solo el DSP worker escribe. Simulation escribe en su propia copia y hace merge ordenado.
- **¿Alineación?** → 16 bytes para SIMD, 64 bytes para evitar false sharing.
