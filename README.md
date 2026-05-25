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

- **Tamaño de portadoras**: se exige `carrier_pixels ≥ 8 · secret_pixels / k`.
  Para `k < 8` esto implica portadoras más grandes que el secreto; para `k > 8`
  alcanza con portadoras más chicas.
- **Justificación**: mantenemos el mismo esquema de ocultamiento (1 LSB por
  píxel, MSB-first), lo que simplifica el código y conserva la
  correspondencia 1-byte-por-bloque-por-portadora del paper. Cada sombra
  ocupa `8 · secret_size / k` bits = `secret_size / k` bytes embebidos en
  los LSBs de las primeras `8 · secret_size / k` portadoras de la
  portadora.
- **Propuesta descartada**: usar múltiples LSBs por píxel (por ejemplo, 4
  LSBs para `k=2`, 2 LSBs para `k=4`) para mantener portadoras del mismo
  tamaño que el secreto. La descartamos porque el deterioro visual de la
  portadora crece y el código de embedding/extracción se complica para cada
  `k` distinto, mientras que usar un LSB uniforme deja todo simétrico y
  homogéneo.

### Limitaciones actuales

- **`recovery()` sólo soporta k = 8** (el caso explícitamente fijado por la
  consigna). Para `k ≠ 8` el programa retorna un error claro. Implementarlo
  requiere además fijar la convención inversa: cómo deducir las dimensiones
  `(width, height)` del secreto a partir de las de la portadora, ya que sólo
  conocemos la cantidad total de píxeles.

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
6. **Workaround GF(257)**: cuando alguna evaluación cae en 256 (no entra en
   un byte), se prueba reemplazar `a₀` por otros valores en `[0, 255]`
   buscando el más cercano al original que no produzca colisiones. Como hay
   a lo sumo `n ≤ 10` valores "malos" de `a₀`, siempre existe uno usable.
   Esto introduce una distorsión acotada (un byte por bloque afectado).
7. Cada byte resultante se embebe en 8 LSBs consecutivos de la portadora
   correspondiente y se estampa la semilla y el índice de sombra en el
   header.

### Recuperación

1. Se listan y leen las primeras `k` portadoras del directorio.
2. Se valida que tengan iguales dimensiones (requisito del spec para `k=8`).
3. Se extrae la semilla (`bfReserved1`) y los índices de sombra
   (`bfReserved2`) de los headers.
4. Para cada bloque de 8 píxeles de cada portadora se reconstruye el byte de
   sombra leyendo los LSBs (MSB-first).
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
