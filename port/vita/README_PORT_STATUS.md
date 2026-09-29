# Halo CE en PlayStation Vita: Informe Completo de Cambios y Estado del Port

**Fecha de actualización:** 29 de septiembre de 2026  
**Repositorio base:** `cybersecurity/halo-ce-universal` (Commit: `cd47170f39d5a9fd8e49c48dd6a5d704c900c094`)  
**Autor:** Antigravity (Pair Programming con el usuario)  

---

## 1. Respuesta Transparente sobre la Naturaleza de la Recompilación

> **Observación del usuario:** *"Tengo la sensación de que no estamos recompilando el juego sino que coges trocitos pequeños e intentas que sea parecido."*

Tu observación es **100% acertada y comprensible**. Es fundamental ser completamente claros sobre la arquitectura actual de las pruebas:

1. **Lo que SÍ es el juego original y se está usando directamente:**
   - **Formatos y archivos de datos (.map):** Los archivos `ui.map`, `a10.map` y `bloodgulch.map` son los archivos originales de la versión Xbox/PC sin alterar.
   - **Algoritmos de descompresión:** El código de descompresión zlib y el desempaquetado de bloques proviene de `source/memory/zlib/`.
   - **Estructuras de tags:** Las cabeceras de tags (`cache_file_tag_header`), instancias (`cache_file_tag_instance`), definiciones de escenarios (`scenario_definitions.h`) y geometrías de mapa (`structure_bsp_definitions.h`) son las estructuras C exactas del motor original descompilado.
   - **Normales y Geometría 3D:** El algoritmo de descompresión de normales por vértice de 32 bits empaquetados (`uncompress_halo_normal`) es la rutina matemática idéntica de `source/rasterizer/rasterizer_geometry.c`.
   - **Colisiones:** La extracción de planos de colisión, polígonos, aristas y vértices de colisión del BSP es la geometría física del juego original.
   - **Texturas:** Las texturas subidas a la GPU son los bloques DXT1, DXT3, DXT5 y mapas de intensidad AY8 extraídos directamente de los tags `bitm` del archivo de mapa.

2. **Lo que ha sido un "Test Runner / Andamiaje" provisional (`map_test.c`):**
   - Los VPKs generados hasta ahora (desde el `0.2` de diagnóstico hasta el `2.0` de texturas GPU) **no arrancan desde el `main()` completo del motor** (`source/main/main.c`), sino desde un arnés de prueba progresivo situado en `port/vita/src/map_test.c`.
   - **¿Por qué se hizo esto?**
     La configuración `halobetacache` aporta **466 archivos C disponibles**. La auditoría reproducible de los 466 objetos del motor más `xapi_memory.c` encuentra **612 símbolos externos únicos sin resolver**:
     - **106 llamadas a Direct3D 8 / GPU NV2A de Xbox:** Halo no usaba OpenGL ni Vulkan; interactuaba directamente con el microcódigo de DirectX 8 de Xbox.
     - **72 llamadas a la API del Kernel de Xbox / Windows:** Creación de hilos Win32 (`CreateThread`), mutexes, eventos, temporizadores de alta precisión y lectura de archivos.
     - **El sistema de sonido de Xbox (XAudio/DirectSound):** Hardware específico de audio de Xbox.
     - **La memoria virtual fija de Xbox:** Los mapas y el motor esperan que la memoria de tags resida en `0x803A6000`. Vita entrega el arena en una dirección dinámica (en las pruebas, alrededor de `0x81A00000`), por lo que los punteros persistidos en el mapa deben traducirse antes de desreferenciarlos. Un puntero Xbox sin traducir puede terminar la aplicación con un acceso inválido.

3. **Las consecuencias del andamiaje temporal:**
   - Para poder ver el juego en la pantalla de la consola antes de resolver los 612 símbolos de DirectX/Win32, se crearon rutinas de visualización directa en `map_test.c` (el HUD dibujado por código, la cámara en primera persona y los menús provisionales).
   - Como bien señalaste, ese HUD y menús temporales no tenían la fidelidad del juego original y se sentían como un apaño.

---

## 2. Inventario Completo de Cambios en el Repositorio de Codex

A continuación se detalla cada archivo que ha sido modificado o añadido en la copia de trabajo respecto al commit `cd47170` de `cybersecurity/halo-ce-universal`.

### A. Archivos del Motor Original Modificados (10 archivos)

Estos archivos se modificaron para permitir que el código C original de Halo compile limpiamente para la arquitectura **ARMv7-A (Cortex-A9)** de la PS Vita con ABI de punto flotante por hardware (VFPv3-D16 / NEON) y punteros de 32 bits:

1. **`source/cache/physical_memory_map.c`**
   - **Cambio:** Se eliminaron las direcciones base absolutas fijadas a `0x803A6000` (Xbox) y se integró con `halo_vita_address()`.
   - **Motivo:** En Vita el área de juego (Game State) y la caché de tags deben asignarse dinámicamente en un bloque `USER_RW` de 128 MiB.
2. **`source/cache/cache_files.c`**
   - **Cambio:** En `cache_files_enable_writes` y `cache_files_disable_writes`, se cambió `(void *)0x803A6000` por `physical_memory_get_tag_cache_base_address()`.
   - **Motivo:** Evitar accesos a memoria fuera de rango durante la protección de páginas de tags.
3. **`source/saved games/game_state.c`**
   - **Cambio:** Se adaptó `game_state_initialize()` para inicializar el búfer de estado del juego usando la base reubicable de Vita.
4. **`source/interface/terminal.c`**
   - **Cambio:** En `terminal_printf`, se reemplazó la declaración x86 `char *arglist` por el tipo estándar `va_list`.
   - **Motivo:** En arquitecturas ARM los argumentos variádicos se pasan en registros `r0-r3` y registros `d0-d7`, por lo que un puntero char crudo corrompía la pila.
5. **`source/hs/hs.c`**
   - **Cambio:** En las funciones `player_effect_screen_fade_in_evaluate` y `player_effect_screen_fade_out_evaluate`, se corrigió la conversión de argumentos pasando punteros a float (`*(real const *)&arguments->value0`).
   - **Motivo:** Corrección de la convención de paso de parámetros float en ARMv7.
6. **`source/bitmaps/libtiff/tiffcompat.h`**
   - **Cambio:** Se añadió `#include <stdlib.h>` bajo `#ifdef HALO_VITA` en vez de redefinir prototipos antiguos de `malloc` y `realloc`.
7. **`source/memory/zlib/zutil.h`**
   - **Cambio:** Se añadió la directiva `HALO_VITA_STANDALONE_ZLIB` para permitir enlazar las rutinas de descompresión de mapas sin colisionar con dependencias externas.
8. **`port/linux/include/wchar.h`**
   - **Cambio:** Se aisló el tipo `wint_t` de 16 bits de Xbox mediante `halo_vita_wint_t` para evitar conflictos con el `wint_t` de 32 bits de Newlib (la librería C estándar de VitaSDK).
9. **`port/linux/include/StdDef.h`**
   - **Cambio:** Definición de tipos fundamentales enteros para Clang en entorno cruzado.
10. **`port/linux/include/halo_linux_prefix.h`**
    - **Cambio:** Inclusión condicional de cabeceras de soporte para la compilación cruzada.

---

### B. Módulo de Plataforma Nuevo: `port/vita/` (Añadido íntegramente)

Se creó la carpeta `port/vita/` con la infraestructura específica para PlayStation Vita:

* **`port/vita/CMakeLists.txt`:**
  - Configuración de CMake para el compilador cruzado `arm-vita-eabi-gcc` y Clang.
  - Enlace con las librerías nativas de Vita: `vitaGL`, `vitaShaRK`, `mathneon`, `SceGxm_stub`, `SceDisplay_stub`, `SceCtrl_stub`, `SceAppMgr_stub`, etc.
  - Empaquetado automático en formato `.vpk` mediante `vita_create_vpk` con LiveArea completo.
* **`port/vita/compile_probe.py`:**
  - Script de compilación que compila los **466 archivos C del motor original de Halo** usando Clang con flags específicos para ARMv7 hard-float (`-mcpu=cortex-a9`, `-mfpu=neon`, `-mfloat-abi=hard`, `-fshort-wchar`, `-fno-short-enums`).
  - Con `--all`, demostró que los 466 archivos C disponibles del motor compilan individualmente para la CPU de Vita; `xapi_memory.c` eleva el resultado de la encuesta a 467/467 objetos. Esto no implica que el motor completo ya enlace o ejecute.
* **`port/vita/engine_link_audit.py`:**
  - Resta de forma reproducible los símbolos definidos entre esos 467 objetos y genera `engine-link-audit.json` y `engine-link-audit.md` con cada símbolo externo pendiente, su categoría y los módulos que lo referencian.
* **`port/vita/src/arena.c` y `include/halo_vita_memory.h`:**
  - Implementación de un asignador de memoria virtual de 128 MiB basado en `sceKernelAllocMemBlock` de Vita.
  - Mapea las regiones del motor de Halo:
    - `0x00000000` - `0x003A6000`: Búferes del sistema.
    - `0x003A6000` - `0x019A6000`: Tag Cache (22 MiB).
    - `0x01A00000` - `0x02A00000`: Game State (16 MiB).
    - `0x02A00000` - `0x08000000`: Caché dinámico de texturas y sonidos.
* **`port/vita/src/xapi_memory.c`:**
  - Emulación de las funciones de memoria del sistema operativo de Xbox (`XPhysicalAlloc`, `XPhysicalFree`, `XQueryMemoryProtect`) sobre el arena de Vita.
* **`port/vita/src/xapi_platform.c`:**
  - Primera capa del sistema para el arranque del motor: traducción de rutas Xbox, archivos síncronos y operaciones overlapped inmediatas, eventos, mutexes, esperas, hilos, temporización de alta resolución y memoria Win32 básica.
  - Compila con el ABI del juego y elimina 46 símbolos de la línea base original. Sus 31 referencias nuevas son servicios normales de VitaSDK/libc que se resolverán al enlazar el ejecutable.
* **`port/vita/src/core_test.c`:**
  - Harness de prueba que verifica la integridad de memoria y el CRC del motor (`source/memory/crc.c`).
* **`port/vita/src/map_test.c`:**
  - Arnés de ejecución gráfica y física progresivo (utilizado para generar los VPKs de prueba).
  - Incluye descompresión inflada en streaming, reubicación segura de punteros de tags, extracción de colisiones del BSP, renderizado GPU con VitaGL, desempaquetado de normales originales y texturas DXT.
* **`port/vita/sce_sys/`:**
  - Artefactos visuales oficiales para la LiveArea de PS Vita (`icon0.png`, `bg.png`, `startup.png`, `template.xml`).

---

### C. Herramientas y Entorno Configurados en `work/`

Para que el proyecto pudiera compilarse sin depender de emuladores ni entornos externos, se instaló y configuró localmente:
1. **VitaSDK Snapshot (Septiembre 2026):** Toolchain GCC 15.2.0 + binutils para ARM-Vita.
2. **vitaGL (Rinnegatamante):** Capa acelerada por hardware GXM para renderizado OpenGL en PS Vita.
3. **vitaShaRK & SceShaccCgExt:** Compilador de shaders CG en tiempo de ejecución de PS Vita.
4. **libmathneon:** Rutinas matemáticas vectoriales aceleradas por ensamblador NEON.
5. **libtoloader & taihen:** Soporte para carga de módulos y extensiones.

---

## 3. Estado Comparativo: Arnés de Pruebas vs. Motor Completo

| Característica | Arnés de Pruebas Actual (`map_test.c`) | Motor Completo de Halo CE (`source/main/main.c`) |
| :--- | :--- | :--- |
| **Archivos fuente vinculados** | `map_test.c` + zlib + `physical_memory_map.c` + `crc.c` + `xapi_memory.c` | Los 466 archivos C de `source/` |
| **Carga de mapas (.map)** | Directa vía zlib + rebase dinámico en RAM | Subsistema `cache_files.c` original |
| **Geometría 3D** | Malla visual del Structure BSP del mapa original | `rasterizer_environment.c` + Lightmaps originales |
| **Normales e iluminación** | Normales desempaquetadas del mapa + Half-Lambert | Sistema de renderizado DX8 con shaders dinámicos |
| **Texturas** | DXT1/3/5 y AY8 originales extraídas de tags | Mapeador `rasterizer_bitmaps.c` original |
| **Física y Colisión** | Superficies y triángulos originales del BSP | Subsistemas propios de Halo para BSP, objetos y bipeds (`physics/`, `objects/`, `units/`) |
| **Armas y HUD** | Sprites originales sobre HUD 2D nativo | Máquina de estados de armas y HUD completo (`hud/`, `weapons/`) |
| **Inteligencia Artificial** | No activa | Sistema de actores y árboles de comportamiento (`ai/`) |
| **Audio** | No activo | Motor de sonido con streaming ADPCM (`sound/`) |
| **Menús** | Rutas interactivas con bitmaps originales de `ui.map` | Interfaz gráfica Halo UI completa (`interface/`) |

---

## 4. Qué se Necesita para Sustituir el Arnés por el Motor Completo

Para dar el salto definitivo desde el ejecutable de pruebas (`map_test.c`) hacia la compilación del motor completo (`source/main/main.c`) sin recurrir a simulaciones intermedias, se deben completar 4 puentes de ingeniería:

La línea base se obtiene con el objetivo CMake `vita-engine-audit`. La medición inicial inspeccionó 467 objetos, no presentó errores y obtuvo 612 símbolos externos únicos, 106 de ellos con nombre `D3D*`. Tras añadir `xapi_platform.c`, la medición inspecciona 468/468 objetos y resuelve 46 símbolos originales. El total bruto queda en 597 al incorporar 31 llamadas normales a VitaSDK/libc.

1. **Puente Direct3D 8 a VitaGL (`port/vita/src/d3d8_vitagl.c`):**
   - Halo original llama a funciones como `IDirect3DDevice8_CreateVertexBuffer`, `SetVertexShader`, `SetTexture`, `DrawPrimitive`.
   - Hay que implementar una capa ligera (wrapper) que traduzca estas ~106 llamadas a sus equivalentes en VitaGL/GXM. (El port de Android ya tiene un precedente con GLES en `port/linux/src/d3d8_gl.c`, el cual puede adaptarse a VitaGL).

2. **Puente del Sistema Operativo Xbox a Vita OS (`port/vita/src/xbox_system.c`):**
   - Reemplazar las llamadas de hilos de Windows (`CreateThread`, `WaitForSingleObject`) por hilos de Sony (`sceKernelCreateThread`, `sceKernelWaitSema`).
   - Mapear el temporizador de alta resolución a `sceKernelGetProcessTimeWide()`.

3. **Reubicación de Punteros de Tags en Carga:**
   - La lógica de reubicación probada y perfeccionada con éxito en `map_test.c` debe trasladarse a `source/cache/cache_files.c` para que cuando `tag_get()` acceda a un datum, apunte directamente a la memoria física de Vita.

4. **Entrada y Sonido:**
   - Mapear la lectura de botones de `sceCtrlPeekBufferPositive` dentro de `input/input_windows.c`.
   - Conectar la salida del mezclador de sonido a `sceAudioOutOutput()`.

---

## 5. Resumen de Archivos Generados en `outputs/`

Durante el proceso se han generado los siguientes artefactos verificables:

* **Documentación técnica previa:**
  - `viabilidad-halo-vita.md`: Estudio inicial de viabilidad técnica y análisis de dependencias.
  - `halo-vita-core-0.3-notas.md`: Detalles de la primera prueba de CPU y validación de tipos ARM.
  - `avance-halo-vita-0.4.md`: Auditoría de compilación de las 466 unidades C del juego.
* **Parches Git acumulativos:**
  - `halo-vita-core-0.3.patch`: Cambios iniciales de memoria.
  - `halo-vita-avance-0.4.patch`: Ajustes completos de firmas de funciones y ABI.
* **Binarios instalables (.vpk):**
  - `halo-vita-diagnostic.vpk` (0.2): Diagnóstico de hardware de la consola.
  - `halo-vita-core-0.3.vpk`: Comprobación del arena de memoria y CRC.
  - `halo-vita-map-test-0.4.vpk` a `0.8`: Carga progresiva de mapas y menús de UI.
  - `halo-vita-first-person-1.2.vpk` a `1.6`: Colisión física y movimiento por el mapa.
  - `halo-vita-gpu-textures-1.8.vpk`: Primer intento de renderizado de texturas (con los fallos reportados en la foto).
  - `halo-vita-gpu-textures-2.0.vpk`: Versión corregida con texturas repetidas (`GL_REPEAT`), iluminación Half-Lambert sin zonas oscuras y corrección del visor/retícula.
