# SpectraMorph — Spectral Particle Instrument

Resíntesis espectral híbrida (tonal + residual) con partículas espectrales autónomas.
Plugin de audio VST3 / AU / Standalone para macOS.

## Estado — v0.2.0 (Etapa 2)

### Etapa 1 — Live Insert
- STFT, peak detection, partial tracking, resíntesis aditiva + residual
- 4 hilos: Audio / DSP / Simulation / UI
- Uso en Reaper como **inserto** en un track

### Etapa 2 — File Granular (nuevo)
- Carga **WAV** (u otros formatos soportados por JUCE)
- Selector de **segmento** con waveform (drag inicio/fin)
- **Play** preview RT (archivo → `processBlock` → DSP)
- **Export WAV** del segmento procesado
- **TemporalScrambler**: permutación de frames STFT + bin scatter según Coherence/Chaos
- FFT **4096** opcional (Spectral Quality = High)
- Física de simulación desactivada en este modo

Tag UI esperado: `v0.2.0 · granular-file-r6 · <git-sha>`

## Modos de procesamiento

| Modo | Entrada | Uso |
|------|---------|-----|
| **Live Insert** | Audio del track | Reaper / DAW en tiempo real |
| **File Granular** | Archivo + segmento | Standalone recomendado; Load → segmento → Play → Export |

### Coherence / Chaos (ambos modos)
- **Coherence bajo** (knob hacia 0): más orden, frames consecutivos, espectro fiel
- **Chaos alto** (knob hacia 1): permutación temporal, bin scatter, textura inconexa con el origen

## Cómo usar — File Granular

1. Abrir SpectraMorph **Standalone** (o VST con UI)
2. Modo: **File Granular**
3. **Load WAV** → arrastrar handles de segmento en la waveform
4. Ajustar Coherence/Chaos, Fragment ms, Bin Scatter, Seed
5. **Play** para preview (Dry/Wet según knob)
6. **Export WAV** para guardar el resultado

## Cómo usar — Live Insert

1. Insertar en un track, modo **Live Insert**
2. Reproducir audio del track
3. Ajustar knobs (Gravity/Motion activan simulación)

## Compilar

```bash
cmake -S . -B build -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
cmake --build build --parallel 2>&1 | tail -15
```

Xcode (debug):

```bash
cmake -G Xcode -S . -B build-xcode -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
open build-xcode/SpectraMorph.xcodeproj
```

Plugins: `~/Library/Audio/Plug-Ins/VST3/SpectraMorph.vst3` y `.component`

## Tests

```bash
cmake --build build --target SpectraMorph_tests
./build/SpectraMorph_tests
```

Incluye: `scrambler_identity`, `scrambler_chaos`, `file_segment_bounds` + suite previa.

## Documentación

| Archivo | Contenido |
|---------|-----------|
| SPECS_01 | Visión y teoría |
| SPECS_02 | Audio engine |
| SPECS_03 | Pipeline FFT |
| SPECS_07 | Resíntesis |
| SPECS_11 | Interacción musical |
| **SPECS_13** | **Modo Archivo/Granular (Etapa 2)** |
