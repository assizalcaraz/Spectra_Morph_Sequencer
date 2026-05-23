# SpectraMorph — Spectral Particle Instrument

Resíntesis espectral híbrida (tonal + residual) con partículas espectrales autónomas.
Plugin de audio VST3 / AU para macOS.

## Estado actual — MVP Alpha

El pipeline DSP funcional:
- STFT con ventana Hann y noise floor adaptativo
- Detección de picos espectrales
- Partial tracking con fase vocoder (birth / continuation / death)
- Resíntesis aditiva con banco de osciladores
- Mezcla dry/wet

Arquitectura 4 threads:
- **Audio** (hard realtime): processBlock, ring buffers
- **DSP Worker** (soft RT): FFT → peaks → tracking → snapshot → resynthesis
- **Simulation** (normal): physics skeleton, scheduling
- **UI** (normal): editor, visualización, timer

## Cómo usar

1. Carga SpectraMorph como **inserto** en un track de audio
2. Reproduce audio en el track — el plugin procesa la entrada en tiempo real
3. Ajusta los parámetros:

| Control | Función |
|---------|---------|
| Coherence↔Chaos | 0 = estructura armónica, 1 = caos espectral |
| Density | Densidad de partials |
| Tonal/Residual | Balance entre resíntesis tonal y residual |
| Gravity | Fuerza de atracción espectral |
| Motion | Movimiento / deriva de partials |
| Decay | Tasa de decaimiento de energía |
| Spread | Dispersión espectral |
| Dry/Wet | Mezcla señal original vs procesada |

## Compilar

```bash
cmake -B build \
  -DCMAKE_C_COMPILE_OBJECT="<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>"
cmake --build build --parallel
```

Requiere: CMake 3.22+, macOS 13.0+, AppleClang, conexión a GitHub (FetchContent descarga JUCE).

Los plugins se instalan automáticamente a:
- `~/Library/Audio/Plug-Ins/VST3/SpectraMorph.vst3`
- `~/Library/Audio/Plug-Ins/Components/SpectraMorph.component`

## Tests

```bash
cmake --build build --target SpectraMorph_tests
./build/SpectraMorph_tests
```

## Documentación

Las especificaciones detalladas están en los archivos `SPECS_*.md`:

| Archivo | Contenido |
|---------|-----------|
| SPECS_01 | Visión, teoría, estado del arte |
| SPECS_02 | Arquitectura de audio engine |
| SPECS_03 | Pipeline FFT |
| SPECS_04 | Modelo de partículas (N1–N4) |
| SPECS_05 | Física de partículas |
| SPECS_06 | Ecología espectral |
| SPECS_07 | Resíntesis híbrida |
| SPECS_08 | Gestión de fase |
| SPECS_09 | Estructuras de datos y memoria |
| SPECS_10 | Scheduler y degradación |
| SPECS_11 | Interacción musical |
| SPECS_12 | Threading y ownership |
