# SPECS_02 — Audio Engine

## 1. Arquitectura general

4 threads asíncronos desacoplados mediante buffers lock-free y snapshots inmutables:

```
┌──────────────────────────────────────────────────────────────────┐
│  Thread 1: Audio Callback (realtime, prioridad máxima)          │
│                                                                  │
│  input capture → fill ring buffer → mix output buffer → output  │
│  (NO procesamiento DSP, NO simulación, NO I/O)                  │
└──────────────────────────────────────────────────────────────────┘
                           │
                    ring buffer (input)
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│  Thread 2: DSP Worker (prioridad alta)                          │
│                                                                  │
│  FFT → peak detection → partial tracking → noise floor          │
│  → particle birth/update/death → tonal subtraction              │
│                                                                  │
│  output: particle state snapshot (inmutable, double-buffered)   │
└──────────────────────────────────────────────────────────────────┘
                           │
                    snapshot swap (atomic)
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│  Thread 3: Resynthesis (prioridad media)                        │
│                                                                  │
│  read snapshot → additive oscillator bank (tonal)               │
│  + iFFT overlap-add (residual) → fill output ring buffer        │
└──────────────────────────────────────────────────────────────────┘
                           │
                    ring buffer (output)
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│  Thread 4: Simulation (prioridad baja, cadencia variable)       │
│                                                                  │
│  physics update → forces → integration → flocking → collisions  │
│  → decay → lifecycle                                             │
│                                                                  │
│  output: modifies particle state (same double-buffer as DSP)    │
└──────────────────────────────────────────────────────────────────┘
                           │
                    UDP/OSC (best-effort)
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│  Proceso separado: Visual Engine (openFrameworks)               │
│                                                                  │
│  receive state → render → display                               │
└──────────────────────────────────────────────────────────────────┘
```

### Principios de diseño

- El audio callback es el más restrictivo: solo captura/playback + buffer copies.
- El DSP worker es el más pesado: FFT + tracking.
- La resíntesis es periódica y predecible (ideal para CPU cache).
- La simulación es la más flexible: puede saltar frames si el CPU se atrasa.

---

## 2. Thread ownership

| Recurso | Propietario | Acceso desde otros threads |
|---------|-------------|---------------------------|
| Input ring buffer | Audio callback (escritor) | DSP worker (lector, lock-free) |
| Output ring buffer | Resynthesis (escritor) | Audio callback (lector, lock-free) |
| Particle state (snapshot A) | DSP worker (escritor) | Resynthesis (lector, snapshot) |
| Particle state (snapshot B) | Simulation (escritor) | — |
| FFT temporal buffers | DSP worker | Nadie |
| Parameters | APVTS (JUCE) | Todos (thread-safe) |
| Visual state | DSP worker | Communication thread (lock-free queue) |

### Snapshot immutables

La comunicación DSP worker → Resynthesis usa double-buffering:

```
State buffer A: DSP worker escribe, Resynthesis lee
State buffer B: Resynthesis lee, DSP worker escribe

At end of DSP frame:
    atomic_swap(read_ptr, write_ptr)
    Resynthesis starts reading new frame
```

- Resynthesis nunca lee estado a mitad de actualización.
- No necesita locks — solo un atomic pointer swap.
- El contenido del snapshot es plano (array de structs), no un graph.

---

## 3. JUCE Plugin Structure

### Formato inicial
- **VST3** (primario)
- **AU** (macOS secundario)

### Clases principales

| Clase | Thread | Responsabilidad |
|-------|--------|----------------|
| `SPIAudioProcessor` | Callback | processBlock(), parámetros, estado |
| `SPIAudioProcessorEditor` | UI | Componente JUCE |
| `SPIFFTProcessor` | DSP worker | STFT, peak detection, noise floor |
| `SPIPartialTracker` | DSP worker | Partial tracking, N3 lifecycle |
| `SPIResynthesizer` | Resynthesis | Additive bank + iFFT residual |
| `SPIParticleEngine` | Simulation | Física, fuerzas, integración |
| `SPIScheduler` | Gestión | Coordinación entre threads, timing |
| `SPIParameterManager` | Cualquiera | APVTS, automatización |
| `SPICommunicationLayer` | Cualquiera | OSC/UDP al visual engine |

### Scheduler

El `SPIScheduler` corre en el DSP worker thread y gestiona:

- Cuándo ejecutar FFT (cada frame).
- Cuándo ejecutar partial tracking (cada 1-4 frames según estabilidad).
- Cuándo actualizar simulación (cada 1-4 frames, o menos si CPU pressure).
- Cuándo enviar snapshot a resynthesis.
- Timing y logging.

No es un thread separado: es un state machine dentro del DSP worker.

---

## 4. Audio Callback

### Restricciones

- **Latencia máxima**: 10ms (ideal: 5ms).
- **Block size**: 64, 128, 256 samples.
- **Sample rate**: 44.1kHz, 48kHz.
- **Canales**: stereo in / stereo out.

### Flujo

```
processBlock():
  1. Read input buffer
  2. Apply pre-gain / normalization
  3. Copy input → FFT input ring buffer (write)
  4. Copy output ← resynthesis output ring buffer (read)
  5. Mix resynthesis with dry signal según resynth_mix
  6. Update parameter modulation
  7. Increment frame counter
```

NO hace en el callback:
- FFT
- Partial tracking
- Particle updates
- Physics
- Resynthesis
- File I/O
- Memory allocation

### Thread safety

- El callback **NUNCA** bloquea.
- Comunicación con otros threads via `juce::AbstractFifo` (lock-free ring buffer).
- Parámetros via `juce::AudioProcessorValueTreeState` (thread-safe).

---

## 5. Buffering y comunicación

### Ring buffers

| Buffer | Tamaño | Tipo | Productor | Consumidor |
|--------|--------|------|-----------|------------|
| Input audio | 8192 samples | float[] | Audio callback | DSP worker |
| Output audio | 4096 samples | float[] | Resynthesis | Audio callback |
| Particle cmds | 1024 slots | struct[] | DSP worker | Simulation |
| Visual state | 64 slots | struct[] | DSP worker | Comm thread |

### Double-buffering (snapshot)

| Buffer | Tamaño | Productor | Consumidor |
|--------|--------|-----------|------------|
| Particle state A | MaxPartials * sizeof(Particle) | DSP worker | Resynthesis |
| Particle state B | MaxPartials * sizeof(Particle) | Simulation | Resynthesis |

Swap via `std::atomic<State*>`. Sin locks.

### Latency management

- El plugin reporta latencia = `H + N/2` (FFT pipeline).
- Se usa delay compensation (JUCE `setLatencySamples()`).
- La latencia total estimada: ~3.5 × H (ver SPECS_07, sección 9).
- El visual engine opera con latencia adicional de 1-2 frames → no crítica.

---

## 6. Cadencia de simulación

Cada nivel se actualiza a su propia frecuencia (desde SPECS_04):

| Nivel | Frecuencia de update | Thread |
|-------|---------------------|--------|
| N1 (bins) | Cada frame DSP (H samples) | DSP worker |
| N2 (peaks) | Cada frame DSP | DSP worker |
| N3 (partials) | Cada 1-4 frames | DSP worker |
| N4 (clusters) | Cada 4-16 frames | Simulation (post-MVP) |
| Physics | Cada 1-4 frames | Simulation |
| Resynthesis | Cada frame DSP | Resynthesis thread |

El scheduler ajusta dinámicamente: si CPU pressure → reduce frecuencia de partial tracking y simulación.

---

## 7. CPU budget

### Por etapa (estimación para 64 partials, N=2048, H=512)

| Etapa | CPU % (relativo) | Thread |
|-------|-----------------|--------|
| FFT (vDSP) | 5% | DSP worker |
| Peak detection | 5% | DSP worker |
| Partial tracking | 15% | DSP worker |
| Physics (O(P)) | 10% | Simulation |
| Physics (O(P²)) | 20% | Simulation |
| Additive bank | 30% | Resynthesis |
| iFFT residual | 10% | Resynthesis |
| Buffers/overhead | 5% | Todos |

**Total estimado**: ~50-70% de un core moderno a 48kHz.
**Presupuesto**: mantener bajo 80% para evitar dropouts.

### Estrategias de reducción

- Reducir partials (controlable por usuario).
- Reducir frecuencia de simulación.
- Reducir rango de interacción (flocking radius, attraction range).
- Spatial hashing para O(P²) → O(P log P).
- SIMD/Accelerate para additive bank.

---

## 8. Parámetros (MVP consolidado)

| Categoría | Parámetro | Rango | Default |
|-----------|-----------|-------|---------|
| FFT | Window size | 512-8192 | 2048 |
| FFT | Hop ratio | 2-16 | 4 |
| DSP | Birth threshold | 0-1 | 0.1 |
| DSP | Death threshold | 0-1 | 0.01 |
| DSP | Max partials | 16-256 | 64 |
| DSP | Partial max age | 0.1-10s | 2.0s |
| Resynthesis | Resynth mix | 0-1 | 0.5 |
| Resynthesis | Tonal/residual balance | 0-1 | 0.7 |
| Resynthesis | Phase mode | {lock, diffuse, scatter, random} | lock |
| Resynthesis | Diffusion amount | 0-1 | 0.3 |
| Resynthesis | Harmonic locking | {on, off} | on |
| Resynthesis | Tail amount | 0-1 | 0.5 |
| Resynthesis | Transient mode | {protect, diffuse, trigger, kill} | protect |
| Physics | Gravity | 0-10 | 1.0 |
| Physics | Attraction | 0-5 | 0.5 |
| Physics | Repulsion | 0-10 | 2.0 |
| Physics | Damping | 0-1 | 0.95 |
| Physics | Noise amplitude | 0-1 | 0.01 |
| Physics | Flock cohesion | 0-5 | 0.5 |
| Physics | Flock alignment | 0-5 | 0.3 |
| Physics | Flock separation | 0-5 | 1.0 |
| Physics | Wind | -1-1 | 0 |
| Macro | Coherence ↔ Chaos | 0-1 | 0.2 |
| Macro | Spectral centroid | 0-1 | 0.5 |
| Macro | Density | 0-1 | 0.5 |

---

## 9. State management

### Serialización

```
getStateInformation():
  - Valores de todos los parámetros
  - Config del DSP pipeline (N, H, window type)
  - Config de física (constantes de fuerza)
  - Config de resíntesis (modo, balances)
  → MemoryBlock (XML binario)

setStateInformation():
  - Restaurar todo
  - Reset partial tracker
  - Reset FFT pipeline (clear history)
  - Reset simulation
```

### Presets

- Formato: `.spipreset` (XML comprimido).
- Almacenamiento: JUCE `ApplicationProperties` + carpeta de usuario.
- Categorías guía: init, textura, percusivo, drone, granular, noise.

---

## 10. MIDI y control

### MIDI inicial

| Evento | Acción |
|--------|--------|
| Note On | Trigger particle birth/burst en región espectral |
| Note Off | Death acelerado de partículas asociadas |
| Pitch Bend | Frequency shift global |
| CC | Mapeable a cualquier parámetro |
| Aftertouch | Pressure → coherence o density |

### Post-MVP

- MPE (MIDI Polyphonic Expression).
- OSC input para controladores externos.

---

## 11. Logging y debugging

- JUCE `Logger` para debug no-realtime.
- Macros condicionales `#if JUCE_DEBUG`.
- Timers por etapa del pipeline (DSP worker, resynthesis, simulation).
- Dump de estado de partículas (cada N frames) para análisis offline.
- Visualización debug: overlay de partículas sobre espectrograma.

---

## 12. Modos operativos (post-MVP)

| Modo | Window | Partials | Simulación | Latencia | Uso |
|------|--------|----------|------------|----------|-----|
| Live | 1024 | 32 | 4 frames | ~8ms | Performance |
| Studio | 4096 | 128 | 1 frame | ~40ms | Producción |
| Eco | 2048 | 64 | Dinámico | ~18ms | Default |
