# Halo Vita port

Actualización de plataforma 0.2: se corrigen callbacks Ex, apertura sin append,
fechas de archivo, búsquedas y cierre de hilos activos. Se añade XInput Vita y
un VPK separado HVIT00002 que comprueba contratos de plataforma; todavía requiere
ejecución en hardware. Informe: ux0:data/halo-vita-diagnostic/platform-02.txt.

El objetivo `vita-engine-link` combina realmente los objetos y realiza un
intento de enlace con las bibliotecas SDK. Es diagnóstico: no arranca main_loop
ni genera un juego completo. Véase [PLATFORM_STATUS.md](PLATFORM_STATUS.md).
Las cifras de 467/468 objetos más abajo corresponden a mediciones históricas.

El estado actual, las pruebas físicas hasta la versión 2.0 y el plan de
integración del motor están en [README_PORT_STATUS.md](README_PORT_STATUS.md).
La referencia binaria probada está documentada en
[releases/2.0.md](releases/2.0.md).

Para regenerar el inventario completo de dependencias pendientes:

```sh
cmake --build <directorio-build> --target vita-engine-audit
```

El objetivo compila las 466 unidades C disponibles del motor, añade los dos
adaptadores Vita e imprime `TOTAL 468/468 compiled`. Después genera
`engine-audit/engine-link-audit.json` y `.md`. La línea base actual contiene 612
símbolos externos únicos, incluidos 106 símbolos cuyo nombre comienza por
`D3D`. La primera capa de plataforma resuelve 46 de las dependencias originales;
el contador bruto posterior es 597 porque también incluye 31 referencias nuevas
a VitaSDK/libc que se satisfacen durante el enlace final.

## Notas históricas del bring-up 0.3

Base: cybersecurity/halo-ce-universal commit cd47170f39d5a9fd8e49c48dd6a5d704c900c094.

This is a **partial CPU bring-up**, not a playable port. It compiles selected
game units for ARMv7 and links two original Halo modules into a Vita test VPK:
`source/cache/physical_memory_map.c` (with Vita-only relocation changes) and
`source/memory/crc.c` (unchanged). Original assertions remain enabled.

## What is implemented

- Cortex-A9, hard-float ARMv7 object compilation using Clang and VitaSDK headers.
- 32-bit pointers/long/int, signed char, 16-bit game wchar_t, 32-bit game enums.
- A 128 MiB USER_RW arena supplied by Vita, split into page-sized allocations.
- Game state at arena offset 0x01a00000; tag cache at offset 0x003a6000.
- Top-down texture and sound cache allocation within the arena.
- XPhysicalAlloc/Free and XQueryMemoryProtect for the limited CPU test.
- Boundary, overlap, alignment, double/interior free, exhaustion and round-trip
  address-to-offset tests on the host.

The Vita harness calls Halo's allocate, verify, getter and free functions, writes
distinct sentinels in each cache, and checks Halo's raw CRC of `123456789` against
0x340bc6d9. It writes `ux0:data/halo-vita-diagnostic/core-03.txt`.

## Important limits

The allocator is single-threaded. It tracks allocated pages in software; it does
not implement operating-system page protection, write tracking, GPU mapping or
physical contiguity. WRITECOMBINE is accepted only as a hint and the backing
memory remains ordinary USER_RW. Other protection modes are rejected.

This relocates **allocated regions**, not the pointers embedded in map/tag data.
`cache_files.c` still has direct pointers and fixed-address protection calls.
It compiles in the survey but is deliberately not linked into this executable.
It must not be used to load a map yet. A future loader needs structure-aware
pointer relocation, with file bounds checks; blindly changing all 32-bit words
that resemble addresses would corrupt ordinary data.

The rest of the platform layer, renderer, audio, saves, networking and gameplay
are not integrated. Compiling a module does not prove its runtime ABI is correct.
In particular, varargs, struct returns, structure offsets and floating-point
behavior require further audits before adding more engine modules.

SDK-facing C uses VitaSDK GCC/newlib; game-facing C uses Clang with the Xbox type
layout. The current bridge passes only ordinary 32-bit integers and pointers,
plus unsigned-char boolean for display_assert. No enum/wchar data crosses it.
Linker enum/wchar size warnings are suppressed for this narrow interface, not
as evidence of general ABI compatibility. Unused XDK graphics inline functions
are discarded with section garbage collection; graphics calls are not stubbed.

## Build

Requires Python 3, Clang (tested 22.1.2), VitaSDK (tested GCC 15.2.0 snapshot
sdk-snapshot-20260926.790.1), CMake and a native make program. Set VITASDK and add
its bin folder to PATH. From repository root:

```sh
cmake -S port/vita -B build/vita-core -DHALO_CLANG=/path/to/clang
cmake --build build/vita-core -j2
```

On Windows add `-G "MinGW Makefiles"` and optionally set CMAKE_MAKE_PROGRAM.
If Python detection picks the wrong executable, set Python3_EXECUTABLE.
Output: `build/vita-core/halo-vita-core-0.3.vpk`.

The build surveys ten original game modules plus one new XAPI adapter. Individual
compiler warnings/errors are saved in the build's game folder, with
compile-results.json. Only physical_memory_map.o, crc.o and xapi_memory.o are
linked. Other successful objects are not presented as a working engine.

Host tests, using a native C compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror -Iport/vita/include port/vita/src/arena.c port/vita/tests/test_arena.c -o test_arena
./test_arena
```

The host test allocates approximately 128 MiB. It does not use Vita APIs.

## Test on Vita

Install the VPK over diagnostic 0.2 (same title ID HVIT00001). Open **Halo Vita
Core Test**, wait for CORE_TEST=PASS or FAIL, then SELECT + START to exit. Send
core-03.txt, or the partial file and any error code if an assertion terminates
the app. No game data is required; earlier reports remain untouched.

## Validation and provenance

Host allocator tests passed. Ten original game units and the XAPI adapter
compiled. Final ELF includes the real Halo memory/CRC symbols and has no strong
undefined symbols; five optional weak references belong to the toolchain
runtime. The linker reports a missing GNU-stack note in the SDK's crtn.o.
ELF/SELF/VPK generation succeeded. Esta prueba 0.3 fue validada posteriormente
en hardware real; consulte el informe de estado para los hitos posteriores.

vendor/debugScreen* is retained from vitasdk/samples commit
fe8fbef570f3280586c0c20157146e3faefb2181. Plain LiveArea assets were generated for
this test. No proprietary Halo game data is included.
