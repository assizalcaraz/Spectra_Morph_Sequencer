# base.md
# Proyecto: Spectral Particle Instrument (SPI)
## Estado conceptual inicial

---

# 1. Visión general

SPI es un instrumento/plugin de audio experimental basado en:
- análisis espectral FFT/STFT,
- granularidad espectral,
- sistemas de partículas,
- comportamiento emergente,
- resíntesis híbrida,
- visualización performática.

La hipótesis central es:

> “El espectro puede comportarse como una materia viva.”

En lugar de considerar:
- bins FFT como valores estáticos,
- o granos como simples slices temporales,

SPI modela:
- entidades espectrales autónomas,
- con comportamiento temporal,
- interacción colectiva,
- memoria,
- dinámica física y perceptual.

---

# 2. Ontología del sistema

## Unidad básica

Una partícula espectral NO representa necesariamente:
- un bin individual,
- ni un frame completo.

Puede representar:
- un bin,
- un cluster de bins,
- un partial,
- una región tiempo-frecuencia,
- un evento armónico,
- una componente residual.

Cada entidad posee:

- frecuencia
- amplitud
- fase
- energía
- edad
- masa
- velocidad
- posición espectral
- posición espacial
- coherencia
- estabilidad
- afinidad armónica
- historial temporal

---

# 3. Hipótesis artística

SPI no apunta inicialmente a:
- mixing,
- mastering,
- corrección técnica.

Apunta a:
- diseño sonoro,
- performance,
- instalación,
- síntesis experimental,
- live visuals,
- composición textural,
- interacción audiovisual.

---

# 4. Diferencial conceptual

El diferencial NO es:
“usar FFT”.

El diferencial es:

## espectro emergente

donde:
- el espectro evoluciona,
- se autoorganiza,
- muta,
- reacciona,
- colapsa,
- se dispersa,
- genera estructuras dinámicas.

---

# 5. Arquitectura preliminar

## Audio Engine

JUCE:
- plugin host
- DSP
- threading
- parámetros
- automatización
- audio realtime

## FFT Layer

Posibles librerías:
- FFTW
- KissFFT
- Intel IPP
- vDSP (Apple)

## Particle Simulation

Sistema independiente:
- scheduler
- physics
- lifecycle
- interactions

## Visual Engine

openFrameworks:
- render GPU
- partículas
- espectrograma vivo
- geometrías
- interacción visual

Comunicación:
- UDP
- OSC
- shared memory
- ring buffer lock-free

---

# 6. Filosofía de UI

La UI NO debe parecer:
- laboratorio científico,
- plugin médico,
- editor espectral técnico.

Debe sentirse:
- táctil,
- orgánica,
- performática,
- material,
- inteligible.

Inspiraciones:
- fluid simulation
- microscopía
- astronomía
- ecosistemas
- resonancia física
- biología abstracta

---

# 7. Problemas críticos

## DSP

- coherencia de fase
- estabilidad temporal
- smear espectral
- CPU
- latencia
- overlap-add

## UX

- evitar sobrecarga visual
- evitar caos incomprensible
- balancear control y emergencia

## Musicalidad

- evitar “demo cool syndrome”
- mantener reproducibilidad
- generar resultados utilizables

---

# 8. Estrategia de desarrollo

El proyecto debe desarrollarse en capas:

1. prototipo DSP mínimo
2. visualización básica
3. motor de partículas
4. interacción partículas ↔ espectro
5. resíntesis avanzada
6. control performático
7. optimización
8. diseño artístico
9. packaging/plugin

---

# 9. Objetivo técnico inicial

MVP:
- input audio
- STFT realtime
- bins convertidos a partículas
- simulación básica
- resíntesis funcional
- visualización sincronizada

NO buscar:
- calidad comercial inmediata,
- features infinitas,
- presets masivos.

Primero:
demostrar que el paradigma funciona.

---

# 10. Objetivo largo plazo

Construir:
- instrumento audiovisual,
- framework espectral emergente,
- plataforma experimental,
- potencial producto comercial,
- posible instalación performática híbrida.

---

# Lista inicial de documentos SPECS

La siguiente estructura busca separar:
- DSP,
- arquitectura,
- UX,
- investigación,
- pipeline,
- física,
- resíntesis,
- performance.

Cada spec debe evolucionar independientemente.

---

SPECS_01_vision_and_theory.md
- marco conceptual
- referencias académicas
- estado del arte
- hipótesis
- definiciones ontológicas

SPECS_02_audio_engine.md
- arquitectura JUCE
- audio callback
- threading
- realtime constraints
- buses
- buffering

SPECS_03_fft_pipeline.md
- STFT
- ventanas
- overlap
- hop size
- resolución temporal/espectral
- phase handling

SPECS_04_particle_model.md
- definición de partícula
- lifecycle
- atributos
- memoria
- persistencia
- tipos de partículas

SPECS_05_particle_physics.md
- gravedad espectral
- atracción armónica
- colisiones
- flocking
- difusión
- turbulencia
- decaimiento

SPECS_06_spectral_ecology.md
- comportamiento colectivo
- nacimiento/muerte
- mutación
- clusters
- emergent systems
- reglas ecosistémicas

SPECS_07_resynthesis.md
- iFFT
- additive bank
- hybrid synthesis
- noise layer
- coherence retention
- transient preservation

SPECS_08_phase_management.md
- phase vocoder
- phase locking
- phase diffusion
- spectral smear control

SPECS_09_visual_engine.md
- openFrameworks
- GPU rendering
- particle rendering
- shaders
- spectrogram rendering

SPECS_10_ui_ux.md
- interacción performática
- metáforas visuales
- navegación
- ergonomía
- legibilidad

SPECS_11_communication_layer.md
- UDP
- OSC
- synchronization
- latency
- shared state
- lock-free messaging

SPECS_12_realtime_constraints.md
- CPU budget
- SIMD
- cache locality
- GPU offloading
- multithreading

SPECS_13_parameter_system.md
- automation
- modulation
- macro controls
- state serialization
- preset architecture

SPECS_14_control_systems.md
- MIDI
- MPE
- OSC controllers
- gestural control
- sensor integration

SPECS_15_visual_language.md
- estética
- color
- motion language
- biological vs cosmic metaphors

SPECS_16_audio_identity.md
- identidad sonora
- límites estéticos
- casos de uso
- comportamiento musical

SPECS_17_testing_and_metrics.md
- pruebas DSP
- profiling
- métricas perceptuales
- testing subjetivo

SPECS_18_plugin_packaging.md
- VST3
- AU
- CLAP
- installers
- deployment

SPECS_19_research_references.md
- papers
- libros
- plugins
- artistas
- sistemas relacionados

SPECS_20_future_directions.md
- IA
- adaptive systems
- autonomous evolution
- audiovisual ecosystems
- distributed systems

---

# Observación estratégica

El núcleo innovador probablemente NO estará en:
- FFT,
- ni partículas.

Estará en:
- la traducción entre ambos dominios.

Es decir:

cómo una estructura espectral:
- se vuelve materia,
- adquiere comportamiento,
- y vuelve a convertirse en sonido
sin perder musicalidad.
