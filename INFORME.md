# Informe — Secreto Compartido en Imágenes con Esteganografía

**Trabajo Práctico de Implementación — Criptografía y Seguridad (72.04), ITBA, 2026**

Grupo: *(completar integrantes)*

---

## 1. Análisis del documento de Wu y Lo

> Las observaciones sobre estructura, notación y prosa del paper deberían
> revisarse contra el PDF original antes de la entrega.

### 1a. Organización formal

El paper sigue la estructura clásica de un artículo de criptografía:

1. **Abstract**: motivación + síntesis del aporte.
2. **Introducción**: planteo del problema de secreto compartido y de su
   aplicación a imágenes; comparación breve con Naor-Shamir y con
   Thien-Lin.
3. **Esquema propuesto**: definición formal del algoritmo de distribución
   (construcción del polinomio, evaluación, ocultamiento) y del de
   recuperación (extracción, interpolación, inversión de la permutación).
4. **Análisis y resultados experimentales**: pruebas sobre imágenes de
   referencia, métricas de calidad (PSNR de las portadoras modificadas),
   discusión de seguridad.
5. **Conclusión**: cierre y posibles extensiones.
6. **Referencias**.

### 1b. Descripción de los algoritmos

**Distribución**. Dada una imagen secreta de `m` píxeles y un esquema
`(k, n)`:

- Se aplica una **permutación** (en realidad, una transformación
  pseudoaleatoria) sobre los bytes del secreto, sembrada con un valor que
  se ocultará junto con cada sombra.
- Los bytes permutados se agrupan en bloques de `k` y cada bloque se
  interpreta como los coeficientes `a₀, a₁, …, a_{k-1}` de un polinomio
  de grado `k-1` sobre `GF(257)`.
- Para cada sombra `j ∈ {1, …, n}` se evalúa `p(j) mod 257` y se obtiene
  un byte por bloque.
- Esos bytes se embeben en imágenes portadoras mediante reemplazo del
  bit menos significativo (LSB replacement).

**Recuperación**. Dadas cualesquiera `k` de las `n` sombras:

- Se extraen los bytes ocultos invirtiendo el LSB replacement.
- Para cada bloque se forma el sistema lineal `V · c = y` (donde `V` es
  la matriz de Vandermonde de los `x` de las sombras participantes) y se
  resuelve por eliminación de Gauss o interpolación de Lagrange para
  obtener los coeficientes, que son exactamente los bytes permutados.
- Se invierte la permutación original (mismo PRNG con la semilla
  recuperada del header) y se reconstruye la imagen.

### 1c. Notación

En general la notación de los papers de este área tiene puntos
mejorables:

- Mezcla de indexación 1-based y 0-based (los píxeles a veces se numeran
  desde 1, los bytes de archivos siempre desde 0).
- El parámetro `k` se usa tanto para el umbral del esquema como para
  índices de iteración, lo que obliga a inferir el rol por contexto.
- El primo modular no siempre es explícito: en Thien-Lin se usa 251 para
  evitar overflows; en Wu-Lo es 257 (un primo mayor que 256) y eso
  obliga a un workaround porque las evaluaciones pueden caer en 256, que
  no entra en un byte. El paper debería discutir explícitamente esa
  diferencia y su solución.

---

## 2. ¿Es la imagen recuperada exactamente igual a la original?

**No, no es bit-a-bit idéntica**. La razón está en la elección del
módulo primo `257`.

Cada evaluación del polinomio devuelve un valor en `[0, 256]`. Los
valores en `[0, 255]` se pueden almacenar en un byte sin pérdida, pero
si el polinomio produce `256`, no podemos representarlo en 8 bits sin
perder información.

La solución es la que define el paper en su **Step 5**: cuando para
algún `x` la evaluación cae en `256`, se busca el **primer coeficiente
no nulo** del bloque `{a₀, a₁, …, a_{k-1}}` y se lo **decrementa en 1**;
luego se reevalúan **todas** las sombras del bloque y se repite hasta
que ninguna caiga en `256`. El paper prueba que un bloque con todos los
coeficientes en cero nunca produce `256` (evalúa a `0`), así que siempre
existe un coeficiente no nulo para decrementar; además cada iteración
baja en 1 la suma de coeficientes, por lo que el proceso termina. Esto
introduce una **pequeña pérdida**: el coeficiente decrementado es un
byte del secreto, que se recupera con un valor levemente distinto al
original.

En nuestras pruebas con `Albertssd.bmp` (300×300) y `k=8` la
distorsión fue del **0.39 % de los píxeles** (355 de 90 000), con
desvíos chicos en escala de grises — imperceptibles a simple vista.

Una alternativa para evitar la pérdida sería usar `MOD = 251` (como en
Thien-Lin), pero entonces hay que pre-procesar la imagen secreta para
clipear todos los valores mayores a `250`, lo que también pierde
información (esta vez en zonas claras).

---

## 3. Seguridad de la ubicación del número de sombra y de la semilla

En la implementación actual:

- La **semilla** se guarda en los bytes 6-7 del header BMP
  (`bfReserved1`).
- El **índice de sombra** se guarda en los bytes 8-9 (`bfReserved2`).

### Riesgos del esquema actual

1. **Predictibilidad absoluta**: cualquiera que conozca el formato BMP
   puede leer esos cuatro bytes sin esfuerzo. Es esteganografía "por
   oscuridad" muy débil.
2. **La semilla en claro derrota la permutación**: el shuffling es la
   única defensa contra ataques estadísticos sobre los LSBs de una
   portadora aislada. Con la semilla expuesta, un atacante con una
   sola portadora puede generar la misma máscara y aplicar análisis de
   correlación.
3. **Integridad nula**: nada impide que un atacante altere los reserved
   bytes (o el cuerpo de píxeles) y el receptor lo note.

### Alternativas que mejorarían la seguridad

| Estrategia | Cómo ayuda |
|---|---|
| **Cifrar la semilla** con una clave compartida fuera de banda | Romper el esquema requiere también romper el cifrado de la semilla. |
| **Distribuir la semilla con otro `(k, n)`** | La semilla solo es reconstruible si se reúnen `k` portadoras: misma propiedad de threshold. |
| **Embeber la semilla y el índice en los LSBs** (no en el header) | Mucho menos obvio que los reserved bytes; cualquier inspección casual del header no la encuentra. |
| **Derivar la semilla determinísticamente** desde una passphrase ingresada en `recovery` (KDF) | La semilla no se guarda; cualquier herramienta de inspección de archivos no la encuentra. |
| **Agregar un MAC** (HMAC-SHA256 sobre los bytes de píxeles con clave compartida) | Detecta manipulación o portadoras ajenas al esquema. |

Una elección "ingeniería sensata" sería combinar: derivar la semilla
de una passphrase, embeber el índice de sombra en LSBs específicos
(por ejemplo los primeros 8 píxeles, antes del cuerpo de la sombra), y
agregar un MAC al final del cuerpo embebido. Pero todo esto agrega
complejidad operativa que el spec no exige.

---

## 4. Criterio elegido para `k ≠ 8`

El enunciado fija el comportamiento solo para `k = 8`. Para los demás
valores la decisión queda a criterio del grupo.

### Elección final

Aplicamos el **mismo esquema de embedding** (1 LSB por píxel,
MSB-first) que para `k = 8`, y exigimos:

```
carrier_pixels  ≥  8 · secret_pixels / k
```

Es decir, las portadoras deben tener al menos `8/k` veces el tamaño
del secreto:

| `k` | Tamaño mínimo de portadora |
|---|---|
| 2 | 4 × secreto |
| 4 | 2 × secreto |
| 8 | igual al secreto *(impuesto por el spec)* |
| 10 | 0.8 × secreto |

### Justificación

- **Uniformidad de código**: la rutina de embedding/extracción es la
  misma para todos los `k`, no depende del valor del threshold.
- **Mínimo deterioro visual**: usar siempre 1 LSB por píxel garantiza
  que cada portadora pierda como mucho `±1` en el valor de cada píxel
  modificado, lo que se mantiene imperceptible.
- **Las propiedades criptográficas del esquema no dependen del tamaño
  relativo** — solo dependen de `k` y `n`, así que sobredimensionar
  las portadoras no compromete seguridad, solo capacidad.

### Propuesta descartada

Considermos usar **múltiples bits por píxel** (4 LSBs para `k=2`, 2
LSBs para `k=4`, 1 LSB para `k ≥ 8`) para que las portadoras siempre
tuvieran el mismo tamaño que el secreto, replicando el caso `k = 8`.

La descartamos porque:

1. El deterioro visual de las portadoras crece linealmente con la
   cantidad de bits modificados (4 LSBs implican cambios de hasta ±15
   por píxel, visibles en zonas planas).
2. El embedding/extracción se complica: hay que parametrizar la
   cantidad de bits según `k`, manejar casos no divisores limpios
   (¿qué pasa con `k=3` ó `k=5`?), y la deducción de tamaño en
   recovery se vuelve menos directa.

Una propuesta intermedia que también consideramos pero no
implementamos: usar 1 LSB por píxel con portadoras del **mismo tamaño**
que el secreto, lo que limitaría el esquema a `k ≥ 8`. Es la más
simple, pero deja inutilizable la mitad del rango `k ∈ [2, 10]`.

### Limitación actual

`recovery()` está implementada solo para `k = 8`. La extensión a otros
`k` requiere también fijar cómo se deducen las dimensiones
`(width, height)` del secreto desde las de la portadora (en `k = 8`
son iguales; para `k ≠ 8` solo sabemos el total de píxeles, no cómo
distribuirlos). El criterio razonable sería guardar las dimensiones
del secreto en algún campo del header durante distribución, pero el
spec no lo pide explícitamente.

---

## 5. Discusión del algoritmo implementado

### 5a. Facilidad de implementación

El algoritmo en sí es elegante y no excesivamente complejo, pero la
implementación en C exige cuidado en varios frentes:

**Lo simple**:

- La aritmética modular es básica (suma, producto, exponenciación
  rápida para el inverso por Fermat).
- La interpolación se reduce a resolver un sistema lineal `k × k`, lo
  que con `k ≤ 10` es trivial computacionalmente.

**Lo molesto**:

- **El formato BMP**: el header tiene padding por filas a múltiplos de
  4 bytes, las imágenes pueden ser top-down (`biHeight < 0`) o
  bottom-up (default), `bfOffBits` puede o no incluir paleta, y los
  enteros son little-endian. Cualquier descuido genera lecturas
  desplazadas que son difíciles de debuggear.
- **El workaround mod 257**: descubrir y manejar el caso de overflow
  no es obvio leyendo solo el enunciado; pide entender por qué `257`
  no es `≤ 255`. Nuestro primer intento de implementación abortaba
  ante colisiones "irreducibles" y tuvimos que extender la búsqueda a
  todos los `a₀` posibles, no solo decrementándolo.
- **Coordinación distribución ↔ recuperación**: muchas decisiones de
  implementación (orden de los bits dentro del byte, orden de las
  portadoras, formato de la permutación) deben ser exactamente
  inversas entre las dos rutinas. Un cambio en una requiere el cambio
  espejo en la otra.
- **Gestión de memoria**: muchos arrays dinámicos (paletas, píxeles,
  paths de archivos, portadoras), todos con liberación correcta en
  caminos de error. Usamos un patrón de `goto cleanup` centralizado
  para no leakear si algo falla a mitad de camino.

### 5b. Extensión a imágenes en color (24 bpp)

El algoritmo es directamente extensible:

- Un BMP 24 bpp tiene 3 bytes por píxel (B, G, R en ese orden, sin
  paleta). El header es prácticamente igual al de 8 bpp, salvo que no
  hay sección de paleta y `bfOffBits` cae típicamente en 54.
- Hay tres caminos razonables para aplicar el esquema:

  1. **Por canal independiente**: tratar la imagen como tres planos
     (R, G, B) y aplicar el algoritmo completo a cada uno por separado.
     Triplica el tamaño de las sombras pero no toca el algoritmo
     central — solo el módulo de I/O. **Es la opción más limpia**.
  2. **Stream único**: tratar los `3 · m` bytes como una secuencia
     plana. Funciona pero pierde la estructura del color (mezcla bytes
     de canales distintos en el mismo polinomio), lo cual no afecta
     la correctitud pero hace al esquema menos "natural" desde el punto
     de vista de la imagen.
  3. **Composición de canales en un solo valor**: convertir cada píxel
     en un entero de 24 bits y trabajar en un cuerpo finito mayor (por
     ejemplo `GF(2^24 + p)` para algún primo `p`). Más eficiente en
     términos de overhead pero requiere repensar la aritmética modular.

Para LSB replacement: con 24 bpp tenemos 3 bits utilizables por píxel
(1 LSB por canal) en vez de 1, lo que **triplica la capacidad** de
embedding. Eso a su vez permite portadoras más chicas o trabajar con
`k < 8` sin necesidad de portadoras gigantes.

---

## 6. Dificultades encontradas

### En la lectura del documento

- El paper combina notación matemática formal con descripción
  algorítmica informal, sin pseudocódigo unificado, lo que obliga a
  reconstruir mentalmente el flujo.
- La justificación del primo `257` aparece como elección puntual sin
  discutir las consecuencias prácticas (overflow), que es el problema
  más espinoso de la implementación.
- Algunas convenciones (orden de bits dentro del byte, orden de las
  portadoras) quedan a interpretación.

### En la implementación

- **Manejo del overflow mod 257**: el caso `fⱼ(x) = 256` no es obvio
  leyendo solo el enunciado; hay que entender por qué `257` puede
  producir un valor que no entra en un byte. Lo resolvimos siguiendo el
  **Step 5** del paper (decrementar el primer coeficiente no nulo del
  bloque y reevaluar, hasta que ninguna sombra caiga en `256`), que es
  la convención del propio algoritmo y garantiza terminación.
- **Orden de las portadoras**: `readdir(2)` no garantiza orden, así
  que distribución y recuperación podrían procesar las portadoras en
  órdenes distintos y romper la correspondencia con los índices de
  sombra. Lo resolvimos ordenando alfabéticamente con `qsort + strcmp`
  en `list_bmp_files`.
- **Manejo del padding BMP**: el primer test con una imagen de ancho
  no múltiplo de 4 reveló que estábamos leyendo bytes de padding como
  si fueran píxeles. Lo arreglamos calculando explícitamente el row
  stride.
- **Liberación de memoria en caminos de error**: muchos `malloc` con
  múltiples puntos de salida. Optamos por un patrón uniforme de
  `goto cleanup` para concentrar la liberación.
- **Distinguir portadoras del secreto en el directorio**: nuestra
  primera versión incluía el secreto en la lista de portadoras y
  evaluaba un polinomio sobre sus bytes, produciendo basura. Lo
  arreglamos pasando explícitamente el basename del secreto como
  `exclude` al listar archivos.

---

## 7. Extensiones y modificaciones que haríamos

1. **Soporte completo de `k ≠ 8` en recuperación**, definiendo cómo se
   recuperan las dimensiones del secreto (probablemente guardándolas en
   bytes adicionales del header durante distribución).
2. **Soporte de imágenes en color (24 bpp)** aplicando el esquema por
   canal (opción 1 de la sección 5b).
3. **Cifrado y/o derivación de la semilla** a partir de una passphrase
   ingresada en runtime, para que no quede expuesta en el header.
4. **Integridad mediante HMAC**: agregar un MAC al final del cuerpo
   embebido para detectar portadoras alteradas o ajenas al esquema.
5. **Compresión previa** del secreto (RLE o similar) para reducir el
   espacio de embedding necesario, especialmente útil para `k < 8`.
6. **Suite de tests automatizada** con casos de roundtrip distribución →
   recuperación para varios valores de `(k, n)` y métricas de
   distorsión.
7. **Refactorizar `distribute` y `recovery` en funciones más chicas**:
   actualmente son funciones largas con varios bloques bien
   delimitados que se beneficiarían de extraerse (parsing de carriers,
   polinomios por bloque, embedding por sombra).
8. **Mejor manejo de errores en CLI**: actualmente algunos errores
   muestran solo mensaje a `stderr`; sería bueno tener códigos de
   salida específicos por tipo de error para scripting.

---

## 8. Situaciones de aplicación

El esquema `(k, n)` de secreto compartido con esteganografía es
adecuado para escenarios donde:

- Se necesita que **múltiples partes autoricen** el acceso a información
  sensible, sin que ninguna por sí sola pueda hacerlo (analogía: las
  dos llaves que se requieren para abrir una caja fuerte).
- Se quiere **distribuir la confianza** entre actores que no se confían
  mutuamente.
- La existencia del secreto debe **disimularse** (las sombras parecen
  imágenes inocuas).

Casos concretos:

| Aplicación | Por qué encaja |
|---|---|
| **Custodia de claves criptográficas críticas** (root CA, claves maestras de cifrado, llaves de wallet) | El compromiso de menos de `k` shares no compromete la clave; permite recuperar ante pérdida parcial. |
| **Multisig en sistemas financieros** | Versión "natural" del esquema: `k de n` firmantes deben aprobar transacciones. |
| **Escrow de documentos legales** | Testamentos, contratos sellados que se abren solo con consenso de varias partes. |
| **Historias clínicas con consentimiento múltiple** | Un registro médico sensible accesible solo con la aprobación de paciente + médico tratante + auditor. |
| **Backup distribuido de claves** | En lugar de guardar copias completas en varios lugares (que multiplican la superficie de ataque), repartir shares: ninguna copia individual es comprometedora. |
| **Difusión encubierta de información en zonas hostiles** | La cobertura esteganográfica permite que las "sombras" viajen como imágenes comunes a través de canales monitoreados. |
| **Voting electrónico** | Una papeleta puede repartirse entre varias autoridades de manera que ninguna conozca el voto individual hasta el escrutinio. |
| **Almacenamiento en la nube sin confianza única** | Repartir las shares entre varios proveedores cloud: ninguno conoce el secreto, pero `k` de ellos coordinados sí. |

En todos estos casos, la combinación de **threshold scheme** (resiliencia
contra pérdida y compromiso parcial) con **esteganografía** (negación
plausible de la existencia del secreto) es lo que diferencia al sistema
de alternativas más simples como cifrar con una sola clave compartida.
