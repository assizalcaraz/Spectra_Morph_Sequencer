# SPECS_11 — Musical Interaction Model

## 0. Premisa

El sistema tiene física, ecología, fase y resíntesis.
Pero un músico no toca "coherence metrics" ni "harmonic affinity".

Este spec define:

- qué controls ve el usuario,
- qué automatiza el sistema,
- qué NO puede controlar el usuario,
- cómo se toca esto en vivo.

---

## 1. Filosofía de interacción

### Principios

1. **Menos es más**: menos parámetros que un synth promedio. La riqueza está en el comportamiento, no en los knobs.
2. **Macro sobre micro**: el usuario controla estados y tendencias, no partículas individuales.
3. **El sistema tiene agencia**: el usuario no necesita mover algo todo el tiempo. El sistema vive solo.
4. **El error es feature**: la irreproducibilidad parcial es parte del instrumento.
5. **Tactilidad**: los controles deben sentirse físicos, no matemáticos.

### Anti-principios

- NO 400 knobs.
- NO páginas de menús escondidos.
- NO "presets que suenan radicalmente distintos sin entender por qué".
- NO "demo cool syndrome" (impresiona 15 minutos, agota en 40).

---

## 2. Macro-controls (MVP)

El panel principal tiene exactamente **8 controles**.

### 2.1 Coherence ↔ Chaos

El macro eje artístico.

| Valor | Comportamiento |
|-------|---------------|
| 0 (Coherence) | Locking fuerte, drift mínimo, harmonic locking on, diffusion 0, protect transients |
| 0.25 | Estable con micro-variaciones |
| 0.5 (Balance) | Textura viva, algo de drift, diffusion media |
| 0.75 | Scatter, drift significativo, harmonic locking off |
| 1 (Chaos) | Random phase, alta diffusion, transients diffuse, estructura colapsa |

**Sensación**: el instrumento va de "cristal" a "viento".

### 2.2 Density

Cuántas partículas hay.

| Valor | Efecto |
|-------|--------|
| 0 | Vacío (solo residual) |
| 0.5 | Medio (64 partials) |
| 1 | Densidad máxima (según CPU budget) |

Controla: `birth_threshold`, `death_threshold`, `carrying_capacity`.
No controla `max_partials` directamente (eso lo decide el scheduler).

**Sensación**: de "escaso" a "enjambre".

### 2.3 Tonal / Residual Balance

| Valor | Efecto |
|-------|--------|
| 0 | Solo residual (textura, ruido, atmósfera) |
| 0.5 | Balance |
| 1 | Solo tonal (partials puros, estructura armónica) |

Controla: `tonal_residual_balance` en resíntesis.

**Sensación**: de "vapor" a "cuerda".

### 2.4 Gravity

Fuerza de atracción espectral.

| Valor | Efecto |
|-------|--------|
| 0 | Partículas libres, sin estructura tonal |
| 0.5 | Atracción moderada, estructura suave |
| 1 | Gravedad fuerte, jerarquía armónica rígida |

Controla: `gravity` (SPECS_05).

**Sensación**: de "gas" a "cristal".

### 2.5 Motion

Velocidad del sistema.

| Valor | Efecto |
|-------|--------|
| 0 | Congelado (frozen state) |
| 0.5 | Movimiento normal |
| 1 | Máxima agitación (turbulencia) |

Controla: `noise_amplitude`, `wind`, `drift_speed`, `temperature`.
Es un meta-control que acelera/desacelera la dinámica del sistema.

**Sensación**: de "hielo" a "tormenta".

### 2.6 Decay

Qué tan rápido mueren las partículas.

| Valor | Efecto |
|-------|--------|
| 0 | Tails infinitos (sustain mode) |
| 0.5 | Decay natural |
| 1 | Las partículas mueren casi inmediatamente |

Controla: `decay_rate`, `max_age`, `death_threshold`.

**Sensación**: de "infinito" a "percussivo".

### 2.7 Spread

Ancho espectral.

| Valor | Efecto |
|-------|--------|
| 0 | Partículas concentradas en rango medio |
| 0.5 | Distribución normal |
| 1 | Partículas dispersas por todo el espectro |

Controla: `spectral_centroid`, `repulsion`, `flocking_radius`, `wind_range`.

**Sensación**: de "punto" a "nube".

### 2.8 Dry/Wet

Mezcla de la resíntesis con el audio original.

| Valor | Efecto |
|-------|--------|
| 0 | Solo audio original (passthrough) |
| 0.5 | Mezcla balanceada |
| 1 | Solo resíntesis |

---

## 3. Controles secundarios (accesibles pero no visibles por defecto)

En un panel expandido o contextual:

| Control | Rango | Default | Conexión |
|---------|-------|---------|----------|
| FFT size | 512-8192 | 2048 | SPECS_03 |
| Hop ratio | 2-8 | 4 | SPECS_03 |
| Birth threshold | 0-1 | 0.1 | SPECS_03 |
| Phase mode | {lock, diffuse, scatter, random} | lock | SPECS_08 |
| Transient mode | {protect, diffuse, trigger, kill} | protect | SPECS_08 |
| Harmonic locking | on/off | on | SPECS_04 |
| Flocking | on/off | on | SPECS_05 |
| Quality mode | {live, eco, studio} | eco | SPECS_10 |
| CPU target | 50-90% | 75% | SPECS_10 |

Total: **8 macro + 10 secundarios = 18 parámetros** visibles.
El resto (30+ parámetros internos) se auto-gestionan.

---

## 4. Qué NO controla el usuario

Esto define la identidad del instrumento tanto como lo que sí controla.

| NO controla | Razón |
|-------------|-------|
| Partículas individuales | El sistema es poblacional, no granular |
| Frecuencias exactas | El sistema decide la organización espectral |
| Número exacto de partials | El scheduler lo gestiona según CPU |
| Comportamiento de nicho | La ecología asigna roles automáticamente |
| Transiciones de macroestado | El usuario mueve el eje, el sistema decide la transición |
| Memoria histórica | El sistema recuerda automáticamente |
| Routing interno | El pipeline es fijo (la arquitectura lo decide) |

El usuario **influye** pero no **determina**.
La agencia es compartida.

---

## 5. MIDI mapping

### 5.1 Default mapping

| MIDI | Destino |
|------|---------|
| CC 1 (Mod wheel) | Coherence ↔ Chaos |
| CC 2 | Density |
| CC 3 | Tonal / Residual |
| CC 4 | Gravity |
| CC 5 | Motion |
| CC 6 | Decay |
| CC 7 | Spread |
| CC 8 | Dry/Wet |
| Note On | Particle burst en región de la nota |
| Note Off | Release de partículas asociadas |
| Pitch Bend | Spectral centroid shift |
| Aftertouch | Motion (pressure → velocity) |

### 5.2 MPE (post-MVP)

Cada nota MIDI controla una región espectral:

- Note pitch → frecuencia central de la región.
- CC74 (timbre) → harmonic affinity de la región.
- CC71 (brightness) → coherence de la región.
- Aftertouch por nota → density local.

Esto permite polifonía expresiva: cada dedo controla una "colonia" de partículas.

---

## 6. Performance gestures

### 6.1 Burst

Gatillado por Note On.

- Crea un grupo de partículas N2 en la región de la nota.
- Las partículas heredan energía y dispersión según velocity.
- Después del burst, las partículas evolucionan según física/ecología.

### 6.2 Freeze

Congela el estado actual del sistema.

- Detiene toda evolución de partials (posición, fase, energía).
- Mantiene el sonido actual como drone.
- Al unfreeze, el sistema retoma desde donde estaba.

### 6.3 Reset

Vuelve el sistema al estado inicial:

- Mata todos los partials (fade out rápido).
- Resetea memoria ecológica.
- Limpia attractors históricos.
- Mantiene parámetros del usuario.

### 6.4 Morph

Transición suave entre dos configuraciones de parámetros.

- El usuario define snapshot A y snapshot B.
- Un control continuo morph entre ambos.
- Útil para cambios de sección en vivo.

### 6.5 Impulse

Golpe de energía espectral.

- Inyecta energía en una región aleatoria o seleccionada.
- Gatilla births, desestabiliza parciales vecinos.
- Efecto: "piedra en el estanque espectral".

---

## 7. Visual feedback (MVP)

El plugin muestra:

1. **Espectrograma** de fondo (input o residual).
2. **Partículas** como puntos brillantes con estela (posición = frecuencia, tamaño = energía, color = harmonic_affinity).
3. **Coherence ↔ Chaos** como barra o gradiente de color de fondo.
4. **Density** como opacidad general de la nube de partículas.
5. **Macroestado** como color de borde o ícono.

No muestra:
- Números.
- Gráficos de barras.
- Texto técnico.

La UI debe sentirse: **orgánica, táctil, legible**.

---

## 8. Presets

### Filosofía

No "presets infinitos que suenan a todo".
Sino **"personajes espectrales"** — configuraciones que definen un comportamiento, no un sonido.

### Categorías mínimas

| Categoría | Coherence | Density | T/R | Gravity | Motion | Decay | Spread |
|-----------|-----------|---------|-----|---------|--------|-------|--------|
| Init | 0.5 | 0.5 | 0.5 | 0.5 | 0.3 | 0.5 | 0.5 |
| Crystal | 0.9 | 0.3 | 0.8 | 0.8 | 0.1 | 0.7 | 0.2 |
| Swarm | 0.3 | 0.8 | 0.4 | 0.2 | 0.7 | 0.3 | 0.6 |
| Wind | 0.1 | 0.4 | 0.1 | 0.0 | 0.6 | 0.4 | 0.8 |
| Pulse | 0.6 | 0.5 | 0.6 | 0.5 | 0.8 | 0.9 | 0.3 |
| Deep | 0.7 | 0.6 | 0.3 | 0.4 | 0.2 | 0.5 | 0.2 |
| Nimbus | 0.4 | 0.7 | 0.2 | 0.3 | 0.5 | 0.6 | 0.7 |

Cada preset es un punto de partida, no un resultado fijo.

---

## 9. Casos de uso musical

### 9.1 Textura ambiental

- Coherence: 0.3-0.5
- Density: 0.4-0.6
- T/R: 0.3-0.5
- Motion: 0.2-0.4
- Decay: 0.3-0.5

El sistema produce capas que respiran lentamente.
El usuario puede dejar de tocar y el sistema sigue vivo.

### 9.2 Percusión espectral

- Coherence: 0.6-0.8
- Density: 0.3-0.5
- T/R: 0.6-0.8
- Motion: 0.5-0.7
- Decay: 0.8-1.0
- Spread: 0.2-0.4

Cada nota es un burst que decae rápido.
El sonido es rítmico, controlado, percusivo.

### 9.3 Colapso y reorganización

- Empezar en Crystal (coherence alta).
- Mover Coherence lentamente hacia Chaos.
- El sistema se desintegra.
- Inyectar un burst (Nota MIDI) y observar si se reorganiza.

### 9.4 Drone evolutivo

- Freeze el sistema.
- Ajustar parámetros lentamente.
- Unfreeze: el sistema "despierta" en la nueva configuración.

---

## 10. Relación con otros SPECS

| SPECS | Conexión |
|-------|---------|
| 01 — Vision | Este spec realiza la filosofía "táctil, orgánica, performática" |
| 06 — Ecology | Define qué controla el usuario vs qué controla el sistema |
| 07 — Resynthesis | Los macro-controls afectan parámetros de resíntesis |
| 10 — Scheduler | Quality mode conecta con degradación |

---

## 11. Preguntas abiertas

- ¿El usuario debe poder "dibujar" attractors en el espectro?
- ¿Morph entre presets debe ser automático (LFO) o manual?
- ¿Cuánto feedback visual es demasiado?
- ¿El instrumento debe tener un modo "aprendizaje" o tutorial?
- ¿Los gestos (burst, freeze, impulse) deben tener shortcut de teclado?
