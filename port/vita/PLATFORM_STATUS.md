# Plataforma 0.2 — validada en hardware

El VPK halo-vita-platform-0.2.vpk usa HVIT00002 y convive con HVIT00001.
Necesita ux0:data/halo/maps/ui.map. Es una prueba de contratos, no gameplay.
Escribe platform-02.txt en ux0:data/halo-vita-diagnostic.

Cambios: callbacks Ex diferidos al hilo emisor durante una espera alertable;
hEvent ignorado en Ex (Halo almacena allí una bandera); posición de archivo
preservada en Ex; aperturas sin append involuntario; búsqueda filtrada;
fechas por RTC; errores por hilo; sondeo de eventos y mutexes; rechazo del cierre
de hilo activo; arreglo de GlobalReAlloc(NULL); entrada Xbox mediante sceCtrl.

La prueba comprueba archivos, lectura del encabezado real de ui.map, búsqueda,
callbacks, eventos, mutexes, hilo suspendido/reanudado, reloj, memoria y mando.
El 29 de septiembre de 2026 se ejecutó en una PS Vita y finalizó con
`PLATFORM_CONTRACT: PASS`. Pasaron todos los contratos, incluida la lectura real
de `ui.map`, la finalización y resultado de hilos, el aislamiento del último
error entre hilos y las lecturas Ex con callback alertable.

El motor incorpora las definiciones COMMON ya existentes del port Linux.
Sus tamaños heredados siguen necesitando validación de ABI y ejecución.
vita-engine-link compila también arena.c y memory_vita.c y guarda comandos,
hashes de objetos, mapa, log de enlace y link-probe.json. El enlace parcial
combina objetos; el enlace final conserva todas las secciones y todavía falla.
El inventario nm y el enlace con bibliotecas son métricas distintas.

Límites pendientes: protección real de páginas, cierre separado de hilos activos,
acceso concurrente a un mismo archivo, ciclo de vida de contextos por hilo,
equivalencia completa Win32 de atributos/rutas/errores y archivos grandes.
VirtualProtect y SetFileAttributesA devuelven fallo explícito.
El mando aún no tiene asignaciones para clicks de sticks, Black o White.
No se ha alcanzado main_loop, cargado ui.map mediante cache_files.c, ni activado
el gameplay original. El siguiente paso es completar la capa CRT que aún reclama
el enlace, reducir los símbolos sin resolver por subsistema y alcanzar la
inicialización del cargador original antes de conectar el renderer VitaGL.
