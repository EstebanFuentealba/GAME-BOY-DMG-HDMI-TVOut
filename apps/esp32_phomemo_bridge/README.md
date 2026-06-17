# XIAO ESP32S3 Phomemo T02 BLE Bridge

Firmware de referencia para el coprocesador Seeed Studio XIAO ESP32S3 usado por la funcion de impresion del RP2040.

## Conexion UART

| RP2040Pi-Zero | XIAO ESP32S3 | Funcion |
| --- | --- | --- |
| GPIO 8 | D7 / GPIO44 RX | Datos de impresion |
| GPIO 9 | D6 / GPIO43 TX | Estado hacia RP2040 |
| GND | GND | Tierra comun |

UART: `921600 8N1`.

## Transporte Bluetooth

El sketch incluido para XIAO ESP32S3 busca un dispositivo BLE cuyo nombre contenga `T02` o `Phomemo`, toma la primera caracteristica escribible encontrada y envia el raster 1-bit recibido desde el RP2040.

La funcion `sendPhomemoRaster()` esta separada porque Phomemo no publica una especificacion oficial del protocolo T02 y algunos firmwares usan dialectos propietarios. La secuencia de comandos incluida esta basada en `iamjackg/esp32-phomemo-gameboy-printer`, que a su vez acredita el protocolo reverse engineered de `vivier/phomemo-tools`.

Importante: `iamjackg/esp32-phomemo-gameboy-printer` usa `BluetoothSerial`, es decir Bluetooth clasico/RFCOMM, no BLE GATT. El ESP32S3 no soporta Bluetooth clasico. Si tu T02 solo acepta RFCOMM, usa un ESP32 clasico como ESP32-WROOM-32 para el puente o cambia el objetivo de hardware.

Si tu T02 requiere UUIDs o comandos propietarios, ajusta:

- `PRINTER_NAME_HINTS`
- `findWritableCharacteristic()`
- `sendPhomemoRaster()`
