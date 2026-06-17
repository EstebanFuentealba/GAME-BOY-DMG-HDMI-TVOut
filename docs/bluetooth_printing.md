# Impresion Bluetooth Phomemo T02

Esta funcionalidad agrega una ruta de impresion independiente del pipeline HDMI:

1. El boton en `GPIO 7` del RP2040 solicita una captura.
2. El RP2040 copia el framebuffer 2bpp visible a una cola dedicada.
3. La cola procesa el trabajo fuera del camino critico de captura/DVI:
   - escala `160x144` a `384x346`;
   - convierte a raster termico 1-bit;
   - aplica dithering Floyd-Steinberg;
   - envia paquetes UART al ESP32-C3 en trozos pequenos.
4. El ESP32-C3 recibe el raster completo y lo envia por BLE a la Phomemo T02, si la unidad expone una caracteristica BLE escribible.
5. El ESP32-C3 devuelve un byte de estado al RP2040.

## Pines RP2040

| GPIO | Uso |
| --- | --- |
| 7 | Boton de impresion, activo en bajo con pull-up interno |
| 8 | UART1 TX hacia ESP32-C3 RX |
| 9 | UART1 RX desde ESP32-C3 TX |

Los buffers de impresion son independientes de los dos buffers de captura HDMI. La cola acepta dos capturas pendientes; si se presiona el boton con la cola llena, el trabajo se descarta y se reporta `print queue full`.

## Protocolo UART RP2040 -> ESP32-C3

UART: `921600 8N1`.

Cada paquete usa:

| Campo | Tamano | Descripcion |
| --- | ---: | --- |
| magic | 4 | `GBPR` little-endian (`0x52504247`) |
| version | 1 | `1` |
| type | 1 | `1=start`, `2=data`, `3=end`, `4=cancel` |
| seq | 2 | Secuencia por trabajo |
| job_id | 4 | Id incremental |
| length | 2 | Bytes de payload |
| crc16 | 2 | CRC-CCITT de header sin magic/crc + payload |
| payload | n | Datos del paquete |

El paquete `start` envia ancho, alto, bytes por fila, flags y tamano total del raster.

## Estados

El RP2040 imprime por UART de debug estos estados:

- `connecting printer`
- `preparing image`
- `sending data`
- `printing`
- `connection error`
- `print completed`

El ESP32-C3 responde al RP2040 con un byte compatible con `print_status_t`: `6` completado, `8` error de conexion, `9` error de transporte.

## Builds y flasheo web

El workflow `.github/workflows/build-release.yml` compila dos firmwares:

- `gameboy_dvi.uf2` para el RP2040.
- `esp32_phomemo_bridge_*.bin` para el ESP32-C3.

En cada tag `v*` o ejecucion manual, esos archivos se suben al Release y tambien se despliega una pagina de GitHub Pages con:

- Instalador RP2040: descarga `gameboy_dvi.uf2` y lo copia a la unidad `RPI-RP2` usando File System Access API.
- Instalador ESP32-C3: usa ESP Web Tools y el `manifest.json` generado por la Action.

Para usarla, configura GitHub Pages con fuente `GitHub Actions` y abre la URL publicada desde Chrome o Edge. Para el RP2040, conecta la placa manteniendo BOOTSEL presionado hasta que aparezca `RPI-RP2`. Si el navegador no permite escribir la unidad, la pagina tambien deja descargar el UF2 para copiarlo manualmente.

## Nota sobre Phomemo T02

Phomemo no publica una especificacion oficial estable para la T02. El firmware ESP32-C3 en `apps/esp32_phomemo_bridge` aisla esa parte en `sendPhomemoRaster()`.

La secuencia T02 implementada esta basada en `iamjackg/esp32-phomemo-gameboy-printer`: inicializa la impresora, centra la imagen, envia bloques raster `GS v 0` de dos lineas y termina con comandos `1f 11`. Ese proyecto acredita el protocolo reverse engineered de `vivier/phomemo-tools`.

Hay una diferencia de hardware importante: el proyecto de referencia usa `BluetoothSerial`, o sea Bluetooth clasico/RFCOMM. El ESP32-C3 solo soporta BLE. Si tu T02 no expone un servicio BLE escribible compatible y solo funciona por RFCOMM, el puente debe moverse a un ESP32 con Bluetooth clasico, por ejemplo ESP32-WROOM-32.
