# SPECS_05 — Particle Physics

## 0. Premisa

La física en SPI no simula el mundo real.
Simula **comportamiento espectral**.

Cada fuerza debe justificarse por su efecto perceptual, no por su realismo físico.
La materia aquí es sonido. El espacio es frecuencia. El tiempo es musical.

---

## 1. Espacio de simulación

### 1.1 Eje frecuencia (espectral)

Las partículas viven en un espacio **log-frequency** perceptual, no lineal.

```text
spectral_pos = log2(freq / freq_min)
```

Rango: 20 Hz – 20 kHz → ~10 octavas → [0, 10] en espacio log2.

Justificación:
- El oído percibe relaciones multiplicativas, no aditivas.
- Las distancias armónicas son uniformes en log (una octava = 1.0).
- El clustering basado en frecuencia lineal no tiene sentido perceptual.

### 1.2 Eje temporal

El tiempo de simulación avanza en unidades de **frames DSP** (cada H samples).

- 1 frame = H / sampleRate segundos.
- Las fuerzas se integran por frame.
- La simulación se actualiza cada 1-4 frames (ver SPECS_04 — frecuencia por nivel).

### 1.3 Eje espacial (visual, opcional)

Las partículas pueden tener posición 2D/3D para visualización.
Este espacio NO afecta la física espectral.

- `spectral_pos` → determina interacciones espectrales.
- `spatial_pos` → determina rendering visual.
- La relación entre ambos es decidida por el usuario o por reglas de mapeo.

---

## 2. Atributos físicos de partícula

### 2.1 Por nivel

| Atributo | N1 (bin) | N2 (peak) | N3 (partial) | N4 (cluster) |
|----------|----------|-----------|--------------|--------------|
| Mass | 0 (sin masa) | 1 | energy * stability | sum(mass N3) |
| Velocity | 0 | Δfreq/frame | Δfreq/frame | centroid drift |
| Position | freq (lineal) | freq (log) | freq (log) | centroid (log) |
| Force | 0 | 0 | acumulada por frame | neta del grupo |
| Energy | magnitud² | magnitud² | energía trackeada | sum(energy N3) |
| Charge | N/A | N/A | harmonic_affinity | cluster_coherence |

### 2.2 Propiedades derivadas

```cpp
float inertia = mass * (1 + stability);    // resistencia al cambio
float temperature = 1 - coherence;          // "agitación" espectral
float charge = harmonic_affinity * 2 - 1;   // -1 (inharm) a +1 (harmonic)
float spectral_density = energy / bandwidth; // concentración espectral
```

---

## 3. Fuerzas

### 3.1 Gravidad espectral (atracción al centro)

Las partículas tienden a caer hacia su armónico más cercano.

```text
F_gravity = -G * harmonic_affinity * (spectral_pos - harmonic_pos)
```

- `G`: constante de gravedad espectral (controlable por usuario).
- `harmonic_pos`: posición log del armónico más cercano.
- Solo afecta partículas con `harmonic_affinity > threshold`.

Efecto perceptual:
- Partículas armónicas se estabilizan en frecuencias musicales.
- Partículas inarmónicas no sienten esta fuerza.
- Controlable: G = 0 → partículas libres. G = alta → estructura rígida.

### 3.2 Atracción armónica (entre partículas)

Partículas con alta harmonic_affinity mutua se atraen.

```text
F_attraction(i, j) = A * harmonic_affinity(i) * harmonic_affinity(j)
                    * (spectral_pos(j) - spectral_pos(i))
                    * exp(-|freq_i - freq_j| / attraction_range)
```

- `A`: constante de atracción.
- `attraction_range`: ancho de la ventana de atracción (en octavas).

Efecto perceptual:
- Formación espontánea de clusters armónicos.
- Las partículas se agrupan por familias espectrales.
- Crea estructura sin programación explícita.

### 3.3 Repulsión (evitar colapso espectral)

Partículas muy cercanas en frecuencia se repelen para evitar que colapsen al mismo bin.

```text
F_repulsion(i, j) = R * (energy_i + energy_j) / |freq_i - freq_j|²
                   * sign(spectral_pos(i) - spectral_pos(j))
```

- `R`: constante de repulsión.
- Solo cuando `|freq_i - freq_j| < min_spacing`.

Efecto perceptual:
- Preserva ancho de banda espectral.
- Evita "montones" de partículas en la misma frecuencia.
- Mantiene legibilidad armónica.

### 3.4 Fricción espectral

Las partículas pierden velocidad gradualmente, como resistencia del medio espectral.

```text
F_friction = -velocity * damping
```

- `damping`: coeficiente de fricción (0 = sin fricción, 1 = máxima).

Efecto perceptual:
- Partículas no se mueven para siempre.
- El sistema tiende a estabilizarse si no hay fuerza externa.
- Sensación de "medio espectral" con densidad.

### 3.5 Ruido de agitación térmica

Todas las partículas reciben pequeñas perturbaciones aleatorias.
La intensidad depende de su `temperature` (1 - coherence).

```text
F_noise = temperature * noise_amplitude * random(-1, 1)
```

Efecto perceptual:
- Las partículas no se quedan perfectamente quietas.
- Micro-variación permanente → textura viva.
- Partículas incoherentes se agitan más.

### 3.6 Turbulencia (post-MVP)

Campos de fuerza espaciales que deforman el espacio espectral.

- `turbulence_intensity`: 0-1.
- Escala espacial: octavas completas o bandas.
- Temporal: lenta (ondas) o rápida (ráfagas).

Efecto perceptual:
- Movimiento espectral colectivo.
- El espectro "respira".
- Sensación de viento o corriente espectral.

---

## 4. Flocking (comportamiento de grupo)

Basado en el modelo clásico de Boids (Reynolds), adaptado al espacio espectral.

### 4.1 Cohesión

Las partículas tienden hacia el centro de masa de sus vecinas espectrales.

```text
F_cohesion = C * (centroid_local - spectral_pos)
```

Vecinas: partículas dentro de `flock_radius` (en octavas).

### 4.2 Alineación

Las partículas tienden a moverse en la misma dirección que sus vecinas.

```text
F_alignment = A * (mean(velocity_neighbors) - velocity)
```

### 4.3 Separación

Las partículas evitan estar demasiado cerca de sus vecinas.

```text
F_separation = S * sum((spectral_pos - neighbor_pos) / |freq - neighbor_freq|)
```

### Condiciones para flocking

- Solo partículas N3 con `harmonic_affinity > flock_threshold`.
- Solo si hay al menos `min_flock_size` partículas en el radio.
- Rango típico: 0.5-3 octavas.

### Efecto perceptual

- Formación de "nubes espectrales" coherentes.
- Comportamiento colectivo sin programación central.
- El espectro se auto-organiza en constelaciones móviles.

---

## 5. Decaimiento

### 5.1 Energy decay

La energía de cada partícula decae naturalmente:

```text
energy(t) = energy(t-1) * decay_rate
```

- `decay_rate`: 0.999 a 0.9999 por frame (depende de la partícula).

### 5.2 Velocity decay

La velocidad se amortigua:

```text
velocity(t) = velocity(t-1) * velocity_damping
```

- `velocity_damping`: 0.95-0.99 por frame.

### 5.3 Age decay

Las partículas envejecen y se vuelven menos estables:

```text
stability(t) = stability(0) * exp(-age / age_decay_constant)
coherence(t) = coherence(0) * exp(-age / age_decay_constant)
```

### 5.4 Death conditions (desde SPECS_04)

```
death if:
    energy < death_threshold
    OR (age > max_age AND stability < min_stability)
    OR coherence < min_coherence
```

---

## 6. Colisiones

### 6.1 Elastic (intercambio de energía)

Dos partículas que ocupan la misma región espectral intercambian energía y velocidad.

```text
if |freq_i - freq_j| < collision_radius:
    swap(velocity_i, velocity_j) * harmonic_affinity_ratio
    energy_i, energy_j = redistribute(energy_i + energy_j)
```

### 6.2 Inelastic (fusión)

Partículas con alta harmonic_affinity mutua pueden fusionarse:

```text
if affinity_match AND energy_similar:
    new_particle.freq = weighted_mean(freq_i, freq_j, energy_i, energy_j)
    new_particle.energy = energy_i + energy_j
    new_particle.mass = mass_i + mass_j
    kill(i), kill(j)
    birth(new)
```

Efecto perceptual:
- Reducción gradual de complejidad.
- Emergencia de partials dominantes.
- Auto-simplificación del espectro.

### 6.3 Fisión (división)

Partículas con alta energía y temperatura pueden dividirse:

```text
if energy > fission_threshold AND temperature > fission_temp:
    daughter_1 = clone(mother, freq + random_offset)
    daughter_2 = clone(mother, freq - random_offset)
    daughter_1.energy = mother.energy * 0.5
    daughter_2.energy = mother.energy * 0.5
    kill(mother)
    birth(daughter_1), birth(daughter_2)
```

Efecto perceptual:
- Multiplicación de textura.
- El espectro se "ramifica".
- Sensación de reproducción orgánica.

---

## 7. Campos externos

### 7.1 Spectral attractors

Puntos en el espacio espectral que ejercen fuerza constante.

```text
F_attractor = attractor_strength * (attractor_pos - spectral_pos)
            * exp(-distance² / attractor_radius²)
```

- Posición configurable por el usuario.
- Múltiples attractores simultáneos.
- Útil para performance: "dibujar" regiones espectrales de atracción.

### 7.2 Repellers

Puntos que repelen partículas.

```text
F_repeller = repeller_strength / distance²
```

### 7.3 Wind (viento espectral)

Fuerza uniforme en una dirección del espacio espectral.

```text
F_wind = wind_strength * wind_direction
```

Efecto: desplazamiento global del espectro hacia agudos o graves.

---

## 8. Integración temporal

### 8.1 Euler simplificado (MVP)

```cpp
void update(N3Particle& p, float dt) {
    Vec2 force = 0;

    // Sumar fuerzas
    force += gravity(p);
    force += attraction(p, neighbors);
    force += repulsion(p, neighbors);
    force += friction(p);
    force += noise(p);
    if (flocking_enabled) force += flocking(p, flock_neighbors);
    if (has_external_field) force += external_field(p);

    // Integrar
    Vec2 acceleration = force / p.mass;
    p.velocity += acceleration * dt;
    p.velocity *= velocity_damping;
    p.spectral_pos += p.velocity * dt;

    // Actualizar frecuencia desde posición espectral
    p.frequency = log2_to_freq(p.spectral_pos);

    // Decaimiento
    p.energy *= decay_rate;
    p.age += dt;
    p.stability *= stability_decay;
}
```

### 8.2 dt (delta time)

```
dt = 1.0 (por frame DSP, normalizado)
```

En la práctica, `dt = frames_since_last_update`.
Si la simulación se actualiza cada 4 frames DSP, `dt = 4`.

---

## 9. Parámetros de física (MVP)

| Parámetro | Rango | Default | Fuerza |
|-----------|-------|---------|--------|
| Gravity (G) | 0-10 | 1.0 | Gravidad espectral |
| Attraction (A) | 0-5 | 0.5 | Atracción armónica |
| Repulsion (R) | 0-10 | 2.0 | Repulsión |
| Damping | 0-1 | 0.95 | Fricción |
| Noise amplitude | 0-1 | 0.01 | Agitación térmica |
| Flock cohesion | 0-5 | 0.5 | Cohesión de grupo |
| Flock alignment | 0-5 | 0.3 | Alineación de grupo |
| Flock separation | 0-5 | 1.0 | Separación de grupo |
| Collision radius | 0-0.5 oct | 0.1 oct | Radio de colisión |
| Fission threshold | 0-1 | 0.8 | Umbral de fisión |
| Fusion affinity | 0-1 | 0.7 | Umbral de fusión |
| Decay rate | 0.99-1.0 | 0.999 | Decaimiento de energía |
| Attractor strength | 0-10 | 0 | Campo externo |
| Wind strength | -1-1 | 0 | Viento espectral |

---

## 10. Física por nivel ontológico

| Nivel | Física aplicable |
|-------|-----------------|
| N1 (bin) | Sin física (puramente FFT) |
| N2 (peak) | Noise + fricción básica |
| N3 (partial) | Todas las fuerzas (completa) |
| N4 (cluster) | Centroide + fuerzas de grupo + reglas emergentes |

La física no escala hacia abajo: N1 no necesita fuerzas. N2 necesita muy pocas.
Solo N3 tiene el modelo completo.

---

## 11. Complejidad computacional

| Componente | Orden | Notas |
|-----------|-------|-------|
| Gravedad | O(P) | Por partícula |
| Atracción/repulsión | O(P²) | Entre pares (puede ser O(P log P) con spatial hashing) |
| Flocking | O(P²) | Vecindad local (optimizable) |
| Ruido | O(P) | Por partícula |
| Colisiones | O(P²) | Solo pares cercanos |
| Decaimiento | O(P) | Por partícula |
| Campos externos | O(P) | Por partícula |

### Estrategia de optimización

- **Spatial hashing**: dividir el espacio espectral en buckets logarítmicos.
- Las interacciones solo se calculan dentro del mismo bucket o vecinos.
- Relevante para P > 128.

---

## 12. Integración con resíntesis (SPECS_07)

La física produce cambios en:

- `frequency` → afecta la frecuencia del oscilador additive.
- `amplitude` (derivada de energía) → afecta ganancia del partial.
- `coherence` → afecta la estrategia de fase (locking vs diffusion).
- `stability` → afecta la probabilidad de que el partial sobreviva.
- `harmonic_affinity` → afecta el harmonic locking.

La física NO produce audio directamente.
Modifica el estado de las partículas que la resíntesis lee en cada snapshot.

---

## 13. Relación con otros SPECS

| SPECS | Conexión |
|-------|---------|
| 07 — Resynthesis | La física actualiza los atributos que la resíntesis consume |
| 04 — Particle model | La física actúa sobre N3; N4 emerge de relaciones físicas |
| 08 — Phase management | Kuramoto coupling como fuerza física entre partials |
| 06 — Spectral ecology | Las reglas ecológicas (birth, death, mutación) son casos de fisión/fusión |

---

## 14. Preguntas abiertas

- ¿La física debe tener un modo "freeze" (detener toda simulación)?
- ¿Deben las partículas tener "memoria de fuerzas" (historial de fuerzas aplicadas)?
- ¿El wind espectral debe ser periódico (LFO) o constante?
- ¿Las colisiones deben sonar (gatillar events) o ser silenciosas?
- ¿Debe haber "gravedad inversa" (partículas huyendo de armónicos)?
- ¿Cómo interactúa la física con el usuario en tiempo real (mouse, tacto)?
