# SPECS_06 — Spectral Ecology

## 0. Premisa

El sistema tiene física (SPECS_05), fase (SPECS_08), ontología (SPECS_04) y resíntesis (SPECS_07).
Lo que falta es **comportamiento sistémico temporal**.

La ecología espectral estudia cómo las partículas:

- compiten por recursos,
- se reproducen y mutan,
- forman nichos,
- desarrollan memoria colectiva,
- estabilizan o colapsan el sistema,
- producen macro-formas temporales.

No es una capa más. Es el **tejido conectivo** entre la dinámica local (física) y la experiencia musical global.

---

## 1. Recursos del ecosistema

Las partículas compiten por recursos finitos.
Sin competencia, no hay ecología — solo simulación decorativa.

### 1.1 Energía espectral

Cantidad total de energía disponible en el sistema en un momento dado.

```
total_energy = sum(partial.energy) + sum(residual_energy)
```

- Fuente: input audio (FFT).
- Consumidores: todas las partículas.
- Renovable: sí, mientras haya input.
- No renovable: en modo sostenido (sin input, solo decay).

### 1.2 Coherencia

La coherencia total del sistema es un recurso compartido que las partículas pueden:

- **Acumular**: partials estables aumentan la coherencia global.
- **Consumir**: partials inestables, diffusion, scatter la reducen.
- **Transferir**: via coherence donation (SPECS_08).

```
global_coherence = mean(partial.coherence) * harmonic_agreement
```

La coherencia no es infinita. Si muchas partículas demandan coherencia simultáneamente, el sistema se vuelve inestable.

### 1.3 Espacio frecuencial

El rango espectral (20 Hz – 20 kHz, ~10 octavas en log-space) es un recurso finito.

- Partículas muy cercanas se repelen (SPECS_05).
- Regiones densas tienen más competencia.
- Regiones vacías son nichos disponibles para colonización.

### 1.4 Tiempo de vida

Cada partícula tiene un `max_age` que consume tiempo del sistema.

- Partículas viejas ocupan espacio que podrían ocupar nuevas.
- La muerte libera recursos.
- La reproducción crea nuevas demandas.

### 1.5 Ancho de banda espectral

Cada partial ocupa una región alrededor de su frecuencia central.
El ancho de banda usado por partial es proporcional a su energía y estabilidad.

```
bandwidth_used = sum(partial.energy * spread_factor)
available_bandwidth = total_spectral_range - bandwidth_used
```

Cuando `bandwidth_used > available_bandwidth`, el sistema se satura: aumenta la repulsión, disminuye la coherencia.

---

## 2. Nichos espectrales

No todas las partículas tienen el mismo comportamiento.
La ecología define **roles** que las partículas adoptan según su contexto.

### 2.1 Drone

| Atributo | Valor |
|----------|-------|
| harmonic_affinity | Alta (0.8-1.0) |
| stability | Alta |
| coherence | Alta |
| drift | Muy bajo |
| life | Largo (10-60s) |
| energy | Media |
| comportamiento | Se ancla a un armónico y persiste |

Rol: sostener estructura armónica estable.
Las drones son la "base" del ecosistema.

### 2.2 Scavenger

| Atributo | Valor |
|----------|-------|
| harmonic_affinity | Baja (0.0-0.4) |
| stability | Baja |
| coherence | Baja |
| drift | Alto |
| life | Corto (0.5-3s) |
| energy | Baja-media |
| comportamiento | Se mueve por el espectro consumiendo energía residual |

Rol: limpiar energía residual, poblar zonas vacías.
Los scavengers evitan que el sistema se sature de micro-energía incoherente.

### 2.3 Predator

| Atributo | Valor |
|----------|-------|
| harmonic_affinity | Media (0.4-0.7) |
| energy | Alta |
| mass | Alta |
| velocity | Alta |
| comportamiento | Se mueve hacia clusters densos y absorbe energía de partials débiles |

Rol: reducir complejidad, forzar reorganización.
Los predators evitan que el sistema se sobre-pueble de partials mediocres.

### 2.4 Absorber

| Atributo | Valor |
|----------|-------|
| harmonic_affinity | Variable |
| coherence | Muy alta |
| stability | Muy alta |
| comportamiento | "Absorbe" inestabilidad de partículas vecinas (phase absorption, SPECS_08) |

Rol: estabilizar regiones del espectro.
Los absorbers actúan como sumideros de caos.

### 2.5 Harmonizer

| Atributo | Valor |
|----------|-------|
| harmonic_affinity | Máxima (0.9-1.0) |
| drift | Cero |
| comportamiento | Actúa como "semilla" armónica: atrae partials cercanos a la serie armónica |

Rol: crear estructura tonal donde no la hay.
Los harmonizers organizan el caos en jerarquías armónicas.

### 2.6 Parasite

| Atributo | Valor |
|----------|-------|
| energy | Baja |
| coherence | Baja |
| comportamiento | Se acopla a un partial con alta energía y desvía su fase/coherencia |

Rol: introducir imperfección controlada.
Los parasites evitan que el sistema suene "demasiado perfecto".

### 2.7 Transient feeder

| Atributo | Valor |
|----------|-------|
| life | Muy corto (0.1-0.5s) |
| energy | Alta (explosiva) |
| stability | Muy baja |
| comportamiento | Nace en onsets, muere rápidamente, energiza parciales cercanos |

Rol: traducir transientes en comportamiento.
Los transient feeders conectan la detección de onset (SPECS_03) con la dinámica ecológica.

### 2.8 Nichos emergentes (post-MVP)

Los nichos anteriores son puntos de partida.
La ecología real debería permitir que **nuevos nichos** surjan por combinación de atributos
y presión selectiva del entorno espectral.

---

## 3. Metabolismo

### 3.1 Consumo de energía

Cada partícula consume energía por frame para mantenerse viva:

```
energy_cost = base_metabolism + mass * metabolic_rate + activity_cost
```

- `base_metabolism`: 0.001 (fracción de energía por frame).
- `metabolic_rate`: 0.01 para scavengers, 0.001 para drones.
- `activity_cost`: proporcional a |velocity|.

Si `energy < energy_cost` por N frames → muerte por inanición.

### 3.2 Alimentación

Las partículas obtienen energía de:

- **Input FFT**: fuente primaria (bins → peaks → partials).
- **Residual**: scavengers pueden consumir energía residual directamente.
- **Absorción**: predators pueden robar energía de partials vecinos.
- **Coherence donation**: absorber recibe energía a cambio de estabilizar.

### 3.3 Metabolismo de coherencia

Las partículas también consumen coherencia:

```
coherence_cost = drift_energy + diffusion_demand + temperature * entropy_rate
```

- Partículas con alta temperatura consumen más coherencia.
- Si la coherencia global es baja, las partículas sufren: aumenta drift, disminuye estabilidad.

### 3.4 Eficiencia metabólica

```
efficiency = energy_gained / energy_spent
```

- Drones: alta eficiencia (poco movimiento, mucha persistencia).
- Scavengers: baja eficiencia (mucho movimiento, poca energía).
- Predators: eficiencia media (inversión en movimiento, recompensa alta si encuentran presa).

---

## 4. Reproducción y mutación

### 4.1 Birth ecológico

No todo birth viene del input FFT.
En la ecología, las partículas pueden reproducirse:

```
if partial.energy > reproduction_threshold
AND partial.age > maturation_age
AND population_density < carrying_capacity:
    offspring = clone(partial)
    mutate(offspring, mutation_rate)
    birth(offspring)
    partial.energy *= 0.5 (costo de reproducción)
```

- `reproduction_threshold`: 0.7 (alta energía necesaria).
- `maturation_age`: 50 frames (no se reproduce recién nacida).
- `carrying_capacity`: límite por octava espectral.

### 4.2 Mutación

Los offspring heredan atributos del progenitor con pequeñas variaciones:

```
offspring.frequency = parent.frequency * (1 + random(-1, 1) * mutation_rate)
offspring.harmonic_affinity = clamp(parent.harmonic_affinity + random(-0.1, 0.1), 0, 1)
offspring.coherence = parent.coherence * (0.9 + random(0, 0.1))
offspring.stability = parent.stability * (0.9 + random(0, 0.1))
offspring.niche = mutate_niche(parent.niche, mutation_rate)
```

- `mutation_rate`: parámetro del sistema (0.001-0.1).
- Alta mutación → diversidad, inestabilidad.
- Baja mutación → linajes puros, estructura estable.

### 4.3 Linajes espectrales

Las partículas heredan un `lineage_id` que permite rastrear descendencia:

```
lineage = {
    id: uint64,
    ancestor_id: uint64,
    birth_frame: uint64,
    mutation_count: uint32,
    niche_history: Niche[]
}
```

Post-MVP: los linajes podrían visualizarse como árboles genealógicos espectrales.

### 4.4 Selección natural

No todas las líneas sobreviven. Criterios de selección:

- Las partículas que ocupan nichos con recursos disponibles sobreviven más.
- Las partículas en regiones sobresaturadas mueren antes.
- Las partículas con alta eficiencia metabólica se reproducen más.

Efecto: el sistema evoluciona hacia configuraciones que maximizan el uso de recursos.

---

## 5. Homeostasis

El sistema necesita mecanismos que eviten:

- **Colapso total**: todas las partículas mueren.
- **Saturación infinita**: demasiadas partículas → CPU explosion.
- **Caos irreversible**: coherencia = 0 sin retorno.
- **Silencio perpetuo**: sin input, el sistema se apaga.

### 5.1 Carrying capacity

Límite máximo de partículas por región espectral:

```
max_partials_per_octave = total_budget / num_octaves
```

Si una octava supera su capacidad: aumento de repulsión, aumento de死亡率.

### 5.2 Energy floor

Mecanismo que evita el colapso total:

```
if total_energy < min_energy_threshold:
    inject_energy(partials_mas_coherentes, energy_floor_amount)
```

El sistema no se apaga nunca: siempre hay un piso de energía que mantiene al menos algunas partículas vivas.

### 5.3 Coherence recovery

Mecanismo que evita el caos irreversible:

```
if global_coherence < min_coherence_threshold:
    increase_harmonic_locking
    reduce_diffusion
    boost_absorber_birth_rate
```

El sistema puede autorregularse para volver a un estado coherente.

### 5.4 Pruning

Mecanismo que evita la saturación:

```
if total_particles > max_particles * 0.9:
    prune_policy = {
        sacrifice: partials con menor energy × coherence,
        protect: partials con alta harmonic_affinity,
        target: reducir a max_particles * 0.7
    }
```

El pruning es ecológico: las partículas más débiles mueren primero.

### 5.5 Energy injection

Para evitar el silencio total en ausencia de input:

```
if no_input_frames > freeze_threshold:
    modo = "sustain"
    inyectar energía mínima a partials existentes
    reducir decay_rate
    mantener coherencia
```

El sistema puede "respirar" por sí mismo durante cortos períodos sin input.

---

## 6. Memoria ecosistémica

### 6.1 Memoria local (por partícula)

Cada partícula N3 tiene historial de 16 frames (SPECS_04).
Pero la ecología necesita más:

- `memory_span`: cuántos frames de historia retiene (16-1024).
- `memory_decay`: cómo envejece la memoria (más vieja = menos peso).

### 6.2 Memoria colectiva

El sistema puede recordar configuraciones pasadas:

```
spectral_memory = {
    region: octava,
    activity_history: float[1024],  // energía promedio por frame
    dominant_niche: Niche,
    coherence_trajectory: float[256],
    last_bloom_frame: uint64
}
```

Uso:
- Regiones con alta actividad pasada atraen más partículas (memoria positiva).
- Regiones que colapsaron son evitadas temporalmente (memoria negativa).

### 6.3 Attractors históricos

Puntos en el espacio espectral que han tenido mucha actividad:

```
historical_attractor = {
    freq: float,
    strength: float (proporcional a actividad pasada),
    decay: float (0.999 por frame — se desvanece lentamente)
}
```

Efecto: el sistema tiende a volver a frecuencias que han sido importantes.
Esto crea "hábitos espectrales" — el instrumento desarrolla preferencias.

### 6.4 Memoria de macroestados

El sistema recuerda transiciones entre macroestados (sección 7):

```
transition_memory[from_state][to_state] = count

// Usado para:
// - Predecir próxima transición
// - Crear ciclos predecibles
// - Romper patrones (anti-hábito)
```

---

## 7. Macroestados

El sistema puede existir en distintos macroestados con transiciones entre ellos.

### 7.1 Estados

| Estado | Coherencia | Densidad | Energía | Comportamiento | Sensación |
|--------|-----------|----------|---------|---------------|-----------|
| **Stable** | Alta | Media | Media | Partículas armónicas, pocos cambios | Tonal, reconocible |
| **Bloom** | Media | Alta | Alta | Explosión de births, alta reproducción, diversify | Riqueza, expansión |
| **Migration** | Media | Variable | Media | Desplazamiento colectivo del espectro | Deriva, viaje |
| **Collapse** | Baja | Baja | Baja | Muertes masivas, coherencia cae, sistema se simplifica | Vacío, silencio |
| **Frozen** | Máxima | Baja | Baja | Partículas congeladas en frecuencia, sin drift, sin muerte | Drone estático, hipnótico |
| **Turbulence** | Muy baja | Alta | Variable | Flocking intenso, colisiones, fisión, caos controlado | Textura densa, viva |
| **Extinction** | — | 0 | 0 | Sin partículas, solo residual | Silencio completo (raro) |

### 7.2 Transiciones

Las transiciones ocurren por:

- Umbrales de recursos: energía baja → collapse, energía alta → bloom.
- Intervención del usuario: forzar transición vía macro-parámetro.
- Acumulación: estrés sostenido → transición abrupta.
- Tiempo: ciclos naturales (bloom → collapse → stable → bloom).

```
Transiciones naturales (sin intervención):

Stable ↔ Bloom (cuando energía sube/baja)
Stable → Migration (drift acumulado)
Migration → Turbulence (estrés de densidad)
Bloom → Collapse (saturación + coherencia baja)
Collapse → Stable (recuperación homeostática)
Stable ↔ Frozen (congelamiento/descongelamiento)
```

### 7.3 Ciclos ecológicos

El sistema puede tener ciclos predecibles:

```
Ejemplo de ciclo típico:

1. Entra input → Bloom (nacen muchas partículas)
2. Saturación → Turbulence (compiten por recursos)
3. Pruning → Migration (partículas se desplazan a regiones vacías)
4. Estabilización → Stable (se forma estructura armónica)
5. Decaimiento → Collapse (sin input, el sistema se apaga lentamente)
6. Energy floor → Stable mínimo (homeostasis mantiene partículas base)
```

Estos ciclos pueden durar segundos o minutos según los parámetros.

---

## 8. Rol del usuario

A medida que el sistema gana autonomía ecológica, el rol del usuario cambia.

### 8.1 Espectro de roles

```
Control total ←──────────────────────────────────────────────→ Autonomía total
     │                                                             │
Conductor                                                  Jardinero
Toca notas                                           Cultiva condiciones
Controla todo                                        Guía, no dirige
```

| Rol | Descripción | Modo de interacción |
|-----|-------------|---------------------|
| **Conductor** | El usuario decide cada aspecto. El sistema ejecuta. | Parámetros directos, MIDI, automatización |
| **Perturbador** | El usuario perturba el sistema y observa cómo responde. | Gestos, transientes, inyección de energía |
| **Jardinero** | El usuario crea condiciones para que el sistema crezca. | Campos externos, attractors, siembra de partículas |
| **Colaborador** | El usuario y el sistema co-evolucionan. | Feedback bidireccional, aprendizaje mutuo |

### 8.2 Modos de control

El instrumento debería soportar múltiples modos:

- **Direct** (conductor): el usuario controla partículas individualmente (MIDI).
- **Gestural** (perturbador): el usuario perturba campos, atractores, viento.
- **Ecological** (jardinero): el usuario define reglas macro y deja que el sistema evolucione.
- **Hybrid**: combinación de los anteriores según la pieza.

### 8.3 Agency compartida

El diseño debe responder:

> ¿Quién controla qué?

Propuesta MVP:

| Aspecto | Control |
|---------|---------|
| Input | Usuario (qué suena) |
| Birth threshold | Usuario (cuándo nacen partículas) |
| Física | Sistema (reglas fijas) |
| Ecología | Sistema (reglas fijas) |
| Coherence↔Chaos | Usuario (macro-eje) |
| Macroestados | Usuario (transición manual o automática) |
| Memoria | Sistema (automática) |

---

## 9. Relación con otros SPECS

| SPECS | Conexión |
|-------|---------|
| 04 — Particle model | N4 (organism/cluster) es el nivel ontológico donde opera la ecología |
| 05 — Particle physics | Las fuerzas físicas son el sustrato; la ecología añade competencia y cooperación |
| 08 — Phase management | Coherence donation/absorption como comportamiento ecológico |
| 07 — Resynthesis | La ecología afecta qué partials sobreviven y cómo suenan |
| 03 — FFT pipeline | Los transientes gatillan transient feeders; los peaks alimentan births |
| 01 — Vision | La ecología es la realización de "espectro como materia viva" |

---

## 10. MVP ecológico

La ecología completa es post-MVP.
Pero hay semillas que deben estar desde el principio:

### Entra en MVP

- **Recursos**: energy tracking, noise floor adaptativo, carrying capacity.
- **Homeostasis**: pruning, energy floor, coherence recovery.
- **Niches**: solo drones y scavengers como roles implícitos (determinados por atributos, no por clase explícita).
- **Macroestados**: solo stable ↔ turbulence, controlado por el eje coherence↔chaos.

### No entra en MVP

- Reproducción y mutación.
- Linajes espectrales.
- Memoria colectiva.
- Attractors históricos.
- Roles explícitos (predator, absorber, parasite, etc.).
- Ciclos ecológicos completos.
- Transiciones automáticas entre macroestados.

### Estrategia

El MVP ecológico consiste en:
1. Sistema abierto (entrada de energía del input).
2. Límites (carrying capacity, energy floor).
3. Auto-limpieza (scavenger-like pruning).
4. Eje coherence↔chaos como control de macroestado.

Con eso, el sistema ya muestra comportamiento sistémico sin la complejidad de ecología completa.

---

## 11. Preguntas abiertas

- ¿La reproducción debe ser sexual (dos progenitores) o asexual (clonación)?
- ¿Los nichos deben ser explícitos (clases) o emergentes (combinación de atributos)?
- ¿El usuario debe poder "dibujar" regiones ecológicas (zonas de alta/baja competencia)?
- ¿Debe haber "especies" que el usuario pueda seleccionar o cargar?
- ¿El sistema debe recordar configuraciones entre sesiones (memoria persistente)?
- ¿Cómo se serializa un ecosistema en un preset?
- ¿Debe haber un modo "jardín" donde el sistema evoluciona sin input?
- ¿La extinción total debe ser posible o prevenida?
