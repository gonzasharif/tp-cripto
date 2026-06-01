# visualSSS

Trabajo Práctico de Implementación — **Criptografía y Seguridad (72.04)**, ITBA, 2026.

Implementación en C del esquema de Secreto Compartido en Imágenes con
esteganografía descripto por Wu y Lo, *"An Efficient Secret Image Sharing
Scheme"*.

El programa permite **distribuir** una imagen secreta BMP de 8 bits en `n`
imágenes portadoras (esquema `(k, n)`), y luego **recuperar** el secreto a
partir de cualquier subconjunto de `k` de esas portadoras.

## Compilación

```
make            # construye el ejecutable ./visualSSS
make clean      # borra binarios y objetos
```

Requiere `gcc` con soporte C99 (no usa librerías externas).

## Uso

```
Distribute: visualSSS -d -secret <image.bmp> -k <number> [-n <number>] [-dir <directory>]
Recover:    visualSSS -r -secret <image.bmp> -k <number>           [-dir <directory>]
```

| Parámetro | Significado |
|---|---|
| `-d` / `-r` | Modo distribución / recuperación. |
| `-secret <img>` | Archivo BMP a ocultar (`-d`) o de salida (`-r`). Debe terminar en `.bmp`. |
| `-k <n>` | Umbral del esquema `(k, n)`. `2 ≤ k ≤ 10`. |
| `-n <n>` | Total de sombras a generar. Sólo con `-d`. Si se omite, se usan todas las BMP del directorio. |
| `-dir <d>` | Directorio donde están las portadoras. Default: directorio actual. |

Hay que respetar el orden de los parámetros tal cual el enunciado.

### Ejemplos

```
# Distribuir clave.bmp en esquema (2, 4) usando imágenes de ./varias
visualSSS -d -secret clave.bmp -k 2 -n 4 -dir varias

# Distribuir clave.bmp en esquema (3, N) usando todas las BMP del directorio actual
visualSSS -d -secret clave.bmp -k 3

# Recuperar secreta.bmp en esquema (8, n) desde ./varias
visualSSS -r -secret secreta.bmp -k 8 -dir varias
```

## Estructura del proyecto

```
visualSSS.c             # Parser de CLI + dispatcher a distribute/recovery.
Makefile

utils/
  utils.[ch]            # Helpers genéricos (listar BMP, validar extensión, etc.)

shamir/
  shamir.[ch]           # Algoritmos distribute() y recovery() (Thien-Lin con GF(257)).
  prng/
    prng.[ch]           # LCG (constantes de java.util.Random) para la tabla de permutación.
  bmp/
    bmp.[ch]            # Lectura/escritura de archivos BMP 8-bit sin comprimir.
```

## Convenciones de implementación

### Esquema (8, n) — caso fijado por la cátedra

- Las portadoras **deben tener las mismas dimensiones** (ancho y alto) que la
  imagen secreta. Si no se cumple, el programa aborta sin modificar nada.
- Ocultamiento: **LSB replacement, 1 bit por píxel**, bits de mayor a menor
  (MSB-first), recorriendo los píxeles en orden de archivo a partir de
  `bfOffBits`.
- La semilla del PRNG (2 bytes) se guarda en `bfReserved1` (bytes 6-7 del
  archivo), tal como pide el enunciado.
- El número de sombra (1..n) se guarda en `bfReserved2` (bytes 8-9).

### Esquema (k, n) con k ≠ 8

El enunciado deja esta decisión a criterio del grupo. Nuestra convención:

- **Tamaño de portadoras**: igual que en `k = 8`, las portadoras **deben tener
  las mismas dimensiones** (ancho y alto) que el secreto. Si no se cumple, el
  programa aborta sin modificar nada.
- **Ocultamiento: LSB replacement, 4 bits por píxel** (nibble alto en el primer
  píxel, nibble bajo en el segundo), recorriendo los píxeles en orden de
  archivo a partir de `bfOffBits`. Así cada byte de sombra ocupa 2 píxeles en
  vez de 8.
- **Por qué entra**: la sombra tiene `secret_size / k` bytes y con 4 LSBs ocupa
  `2 · secret_size / k` píxeles. Como la portadora tiene `secret_size` píxeles
  y `2/k ≤ 1` para todo `k ≥ 2`, la sombra siempre cabe (para `k = 2` usa la
  portadora completa; para `k` mayor sobra espacio sin tocar).
- **Por qué LSB-4 y no LSB-1**: con 1 bit por píxel una sombra `k < 8`
  necesitaría una portadora más grande que el secreto. Subiendo a 4 bits por
  píxel mantenemos **portadoras del mismo tamaño que el secreto** para todo el
  rango `k ∈ [2, 10]`, en paralelo con el caso `k = 8`. El precio es un mayor
  deterioro visual de la portadora (un píxel puede variar hasta ±15, contra ±1
  en LSB-1).
- La semilla y el número de sombra se guardan en el header igual que en
  `k = 8` (`bfReserved1` / `bfReserved2`). Como portadora y secreto miden lo
  mismo, la recuperación deduce las dimensiones del secreto directamente de la
  portadora, sin metadata adicional.

> La regla general que unifica ambos casos es **bits-por-píxel = 8/k**: da
> `1 LSB` para `k = 8` (lo que pide el enunciado) y `4 LSB` para `k = 2`, y en
> todos los casos deja portadoras del tamaño del secreto. El enunciado fijó
> `4 LSB` para todo `k ≠ 8`.

## Detalles algorítmicos

### Distribución

1. Se lee el secreto (BMP 8-bit, `bmp_read`) y se valida que
   `secret_size % k == 0`.
2. Se listan las portadoras del directorio (orden alfabético, determinista)
   y se validan dimensiones según el esquema.
3. Se genera una **semilla de 16 bits** (`time(NULL) & 0xFFFF`) y se inicializa
   el LCG.
4. Se aplica una máscara XOR byte-a-byte sobre el secreto, usando el stream
   pseudoaleatorio (tabla de permutación reversible).
5. Para cada bloque de `k` bytes permutados se construye un polinomio
   `p(x) = a₀ + a₁·x + ... + a_{k-1}·x^{k-1}` y se evalúa en `x = 1..n` mod
   257.
6. **Overflow GF(257)** (Step 5 del paper): cuando alguna evaluación cae en
   256 (no entra en un byte), se decrementa en 1 el primer coeficiente no
   nulo del bloque y se reevalúan todas las sombras, repitiendo hasta que
   ninguna caiga en 256. Un bloque todo-cero nunca produce 256, así que
   siempre hay un coeficiente para decrementar y el proceso termina. Esto
   introduce una distorsión acotada (un byte por bloque afectado).
7. Cada byte resultante se embebe en la portadora correspondiente —en
   **8 píxeles (LSB-1) si `k = 8`** o en **2 píxeles (LSB-4) si `k ≠ 8`**— y se
   estampa la semilla y el índice de sombra en el header.

### Recuperación

1. Se listan y leen las primeras `k` portadoras del directorio.
2. Se valida que tengan iguales dimensiones entre sí (el secreto hereda esas
   dimensiones, para todo `k`).
3. Se extrae la semilla (`bfReserved1`) y los índices de sombra
   (`bfReserved2`) de los headers.
4. Para cada bloque de cada portadora se reconstruye el byte de sombra leyendo
   los LSBs (8 píxeles con LSB-1 si `k = 8`, 2 píxeles con LSB-4 si `k ≠ 8`).
5. Con los `k` pares `(x_i, p(x_i))` se arma el sistema de Vandermonde
   `V · c = y` en GF(257) y se resuelve por **eliminación de Gauss-Jordan**.
   Los coeficientes `c` son exactamente los `k` bytes permutados del bloque.
6. Se deshace la máscara XOR re-inicializando el PRNG con la misma semilla.
7. Se escribe el BMP de salida copiando el header y la paleta de la primera
   portadora (con `bfReserved1/2` limpios), y reemplazando los píxeles.

### Diferencia entre secreto original y recuperado

La imagen recuperada **no es bit-a-bit idéntica** al original por el
workaround del punto 6 de distribución: cada bloque que produjo una
evaluación = 256 termina con un byte ligeramente distinto al original. En
pruebas con imágenes de 300×300, la cantidad de píxeles modificados ronda
el 0.3–0.5%, con desvíos chicos en escala de grises (en general
imperceptibles a simple vista).
