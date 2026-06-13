# SPECS_01 — Visión y Teoría

## 0. Posicionamiento

SPI no es un plugin FFT, un granulador espectral ni un sintetizador additive.
SPI es un **instrumento de materia espectral autoorganizada**.

Su núcleo: el espectro tratado como un medio material cuyas unidades elementales
tienen comportamiento, memoria e interacción — y cuyo sonido emerge de esa dinámica.

---

## 1. Marco conceptual

### Principios fundacionales

1. **Espectro como materia**: los bins FFT no son mediciones — son fragmentos de un medio con masa, velocidad y posición.
2. **Comportamiento sobre estado**: el valor instantáneo importa menos que la trayectoria.
3. **Emergencia**: estructuras musicales complejas surgen de reglas simples a nivel de partícula.
4. **Traducción con pérdida controlada**: la transformación espectro ↔ partículas no necesita ser invertible — necesita ser musicalmente útil.
5. **Jerarquía ontológica**: las partículas existen en niveles (bin → peak → partial → cluster). Cada nivel emerge del anterior. Ninguno lo reemplaza.

### Postura

El sistema no busca transparencia (análisis-resíntesis perfecta).
Busca **viabilidad perceptual**: que el resultado suena a algo con identidad, aunque sea diferente del input.

**Etapa 2 (SPECS_13)** añade modo Archivo/Granular: prioriza texturas espectrales ricas con control explícito de orden vs caos temporal, inspirado en el flujo de segmentación del proyecto Granular_Synth (SuperCollider).

---

## 2. Ontología del sistema

### 2.1 Niveles de partícula

| Nivel | Nombre | Origen | Vida | Resíntesis | Propósito |
|-------|--------|--------|------|------------|-----------|
| N1 | Bin | FFT bin individual | 1 frame | Residual FFT | Base material del espectro |
| N2 | Peak | Peak detection | Multi-frame | Residual / candidato | Energía local estable |
| N3 | Partial | Partial tracking | Segundos | Additive bank | Identidad musical tonal |
| N4 | Organism | Cluster de N3 | Emergente | Colectivo (post-MVP) | Comportamiento espectral de alto nivel |

### 2.2 Atributos por nivel

N3 (el núcleo acústico del sistema):

| Atributo | Tipo | Descripción |
|----------|------|-------------|
| `frequency` | float | Frecuencia central (Hz, log-perceptual internamente) |
| `amplitude` | float | Amplitud (0-1) |
| `phase` | float | Fase desenvuelta (0-2π) |
| `energy` | float | Energía acumulada |
| `age` | float | Tiempo de vida (frames) |
| `mass` | float | Masa = energy * stability |
| `velocity` | float | Derivada de frecuencia (log-space) |
| `spectral_pos` | float | Posición en espacio log-frequency |
| `coherence` | float | Coherencia de fase (0-1) |
| `stability` | float | Estabilidad temporal de frecuencia (0-1) |
| `harmonic_affinity` | float | Afinidad a serie armónica (0-1) |
| `temperature` | float | 1 - coherence (agitación espectral) |
| `history` | ring[16] | Últimos 16 estados de freq/amp |

### 2.3 Relaciones

- **Armónicas**: frecuencias con relación entera (N3 ↔ N3).
- **Vecinas**: proximidad en espacio log-frequency.
- **Clusters**: coherencia mutua + harmonic affinity compartida (→ N4).
- **Familias**: linajes por fusión/fisión (SPECS_05).

---

## 3. Modelo híbrido de resíntesis

Adoptado de Spectral Modeling Synthesis (Serra, 1989) con extensiones:

```
Input
  │
  ├── Tonal (determinista)
  │      partial tracking → additive oscillator bank
  │      → partículas N3 con identidad, fase coherente, comportamiento
  │
  ├── Residual (estocástico)
  │      FFT/iFFT con fase manipulada
  │      → textura, ruido, atmósfera, spectral fog
  │
  └── Transient (discontinuo)
         detección de onset → phase reset o protección
         → ataques, percusión, gestos
```

La decisión híbrida es estratégica:
- additive puro → CPU inviable en altas densidades.
- FFT puro → pierde identidad y vida.
- El split tonal/residual permite emergencia donde importa y textura donde no.

---

## 4. Eje central: Coherence ↔ Chaos

El macro-control artístico del instrumento:

```
Coherence ←──────────────────────────────────────────────→ Chaos
     │                                                       │
Preservación                                            Emergencia
Identidad                                               Descubrimiento
Reproducibilidad                                        Sorpresa
Locking                                                 Diffusion
Partial tracking estable                                Scatter
Fase coherente                                          Fase random
```

No es un parámetro más: es el **lenguaje performático** del instrumento.
El usuario se mueve en este eje como gesto continuo.

---

## 5. Estado del arte

### Sistemas relacionados

| Sistema | Enfoque | Diferencial SPI |
|---------|---------|-----------------|
| Granuladores clásicos | Granos temporales | Granularidad espectral, no temporal |
| Phase vocoder | Manipulación espectral | Agrega comportamiento autónomo |
| Additive synthesis | Partial fijo | Partial vivo con dinámica, drift, interacción |
| SMS (Serra) | Tonal + residual + transients | Agrega persistencia, comportamiento, física |
| Modal synthesis | Modos resonantes | Espectro continuo, no modal |
| Physical modeling | Cuerda/tubo/parche | Modela materia espectral, no cuerda física |
| Particle systems (graphics) | Visual | Mismo modelo aplicado al audio con resíntesis |

### Referencias académicas

- Roads, C. — *Microsound*
- Puckette, M. — *The Theory and Technique of Electronic Music*
- Serra, X. — *Spectral Modeling Synthesis*
- Roebel, A. — *Spectral envelope estimation*
- Xenakis, I. — *Formalized Music*
- Di Scipio, A. — *Ecosystemic models of music*
- Kuramoto, Y. — *Chemical Oscillations, Waves, and Turbulence* (coupling)
- Reynolds, C. — *Flocks, Herds, and Schools* (boids / flocking)

---

## 6. Hipótesis verificables

**H1** — Un sistema de partículas espectrales con reglas simples (gravedad, atracción, fricción, ruido) genera texturas sonoras musicalmente útiles.

**H2** — La resíntesis híbrida (tonal additive + residual FFT) preserva identidad perceptual del material fuente incluso con pérdida de información.

**H3** — La interacción colectiva entre partículas (coherence donation, Kuramoto coupling, flocking) produce comportamientos emergentes no predecibles desde el estado inicial.

**H4** — El eje Coherence ↔ Chaos es un control performático efectivo: permite al usuario navegar entre identidad y emergencia como gesto continuo.

**H5** — El modelo ontológico N1-N4 estructura la complejidad del sistema sin imponer carga cognitiva excesiva al usuario.

---

## 7. Identidad sonora

SPI no busca sonar a:
- un synth cualquiera,
- un efecto FFT genérico,
- un granulador.

Busca sonar a:
- materia espectral en movimiento,
- texturas vivas con comportamiento,
- sonido que respira, deriva, se organiza y colapsa.

El "género" no está definido a priori. Emergerá del diseño del sistema.
Pero el espectro objetivo incluye: textura, drone, pulso espectral, gesto, ruido organizado, masa sonora.

---

## 8. Criterios de éxito

1. El sistema produce sonido en tiempo real (≤ 20ms latency total) con 64 partials.
2. Las partículas N3 son perceptualmente distinguibles de ruido.
3. La resíntesis híbrida preserva identidad del material fuente en modo balanceado.
4. El eje Coherence ↔ Chaos produce variación musical útil, no solo ruido.
5. El comportamiento emergente es repetible (mismo input + mismos parámetros → mismo output).
6. Un músico puede usar el instrumento sin leer los specs.

---

## 9. Preguntas abiertas

- ¿Cuánta incoherencia tolera el oído antes de perder identidad?
- ¿Deben las partículas tener memoria de largo plazo (más allá de 16 frames)?
- ¿El sistema debe tener modos operativos (live/studio) con distintos presupuestos de CPU?
- ¿Cómo se maneja la polifonía real (múltiples fundamentales simultáneos)?
- ¿La visualización debe ser parte del instrumento o accesorio?
- ¿Hasta dónde debe llegar la "autonomía" antes de que el usuario pierda agencia?

---

## 10. Relación con otros SPECS

| SPECS | Dependencia |
|-------|-------------|
| 02 — Audio Engine | Realiza la arquitectura de threads y comunicación |
| 03 — FFT Pipeline | Provee la materia prima (bins, peaks, parciales) |
| 04 — Particle Model | Define la ontología N1-N4 y el lifecycle |
| 05 — Particle Physics | Implementa las fuerzas que mueven las partículas |
| 07 — Resynthesis | Implementa el modelo híbrido tonal/residual/transient |
| 08 — Phase Management | Controla la coherencia del sistema |
