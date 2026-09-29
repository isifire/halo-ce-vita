# Plataforma 0.2 a 3.0 — Enlace Completo del Motor y Paquete VPK

El VPK halo-vita-platform-0.2.vpk usa HVIT00002 y convive con HVIT00001.
Necesita ux0:data/halo/maps/ui.map. Es una prueba de contratos, no gameplay.
Escribe platform-02.txt en ux0:data/halo-vita-diagnostic.

## Hito 3.0: Enlace del Motor Genuino de Halo CE (510/510 Unidades)

El 29 de septiembre de 2026 se completaron los cuatro puentes de subsistemas esenciales para el motor original:
1. **Direct3D 8 a VitaGL (`d3d8_vitagl.c`):** 100% de los 109 símbolos de D3D resueltos y mapeados a VitaGL y PSP2 GXM. Texturas comprimidas DXT1/3/5, vertex/index buffers, render states y pipeline de dibujado.
2. **DirectSound a SceAudio (`dsound_vita.c`):** 100% de los 41 símbolos de audio de Xbox resueltos. Descodificación por hardware de Xbox 4-bit IMA ADPCM y 16-bit PCM, espacialización 3D y mezcla por software enviada a 48 kHz estéreo mediante `sceAudioOutOutput`.
3. **Winsock y XNet a SceNet (`xnet_vita.c`):** Capa de red sobre la pila nativa `SceNet` y `SceNetCtl` de PlayStation Vita, resolviendo todas las llamadas de red distribuida e interpolación de fotogramas.
4. **Enlace y Empaquetado Automático:**
   - 510 de 510 módulos compilados limpiamente con Clang 22.1.2 para ARM Cortex-A9 (`TOTAL 510/510 compiled`).
   - Enlace SDK sin símbolos pendientes: **`unresolved: 0`**, **`duplicate_definitions: 0`**.
   - Generación del ejecutable firmado Sony Vita (`eboot.bin`, ~2.31 MB).
   - Generación del paquete instalable final: `halo-ce-vita.vpk` (~2.29 MB) con LiveArea integrado.
