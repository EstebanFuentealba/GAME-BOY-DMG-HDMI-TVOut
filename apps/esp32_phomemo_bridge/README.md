# XIAO ESP32S3 Phomemo T02 BLE Bridge

Firmware de referencia para el coprocesador Seeed Studio XIAO ESP32S3 usado por la funcion de impresion del RP2040.

## Conexion UART

| RP2040Pi-Zero | XIAO ESP32S3 | Funcion |
| --- | --- | --- |
| GPIO 8 | D7 / GPIO44 RX | Datos de impresion |
| GPIO 9 | D6 / GPIO43 TX | Estado hacia RP2040 |
| GND | GND | Tierra comun |

UART: `921600 8N1`.

## Logs

El firmware imprime logs por USB CDC a `115200` baudios. Para verlos con PlatformIO:

```bash
pio device monitor -d apps/esp32_phomemo_bridge -b 115200
```

Los logs muestran arranque, configuracion UART/BLE, conexion a la impresora, paquetes recibidos desde el RP2040, progreso de impresion, finalizacion y errores.

## Transporte Bluetooth

El sketch incluido para XIAO ESP32S3 conecta por defecto a la impresora por MAC y usa los UUIDs BLE escaneados para la Phomemo T02:

| Campo | Valor |
| --- | --- |
| MAC | `3f:78:0f:5e:07:ef` |
| Servicio | `0000ff00-0000-1000-8000-00805f9b34fb` |
| TX / WRITE | `0000ff02-0000-1000-8000-00805f9b34fb` |
| RX / NOTIFY | `0000ff03-0000-1000-8000-00805f9b34fb` |

La opcion `USE_MAC_ADDRESS` esta activa en `src/main.cpp`. Si quieres buscar por nombre en vez de conectar directo por MAC, cambiala a `false`.

La funcion `sendPhomemoRaster()` esta separada porque Phomemo no publica una especificacion oficial del protocolo T02 y algunos firmwares usan dialectos propietarios. La secuencia de comandos incluida esta basada en `iamjackg/esp32-phomemo-gameboy-printer`, que a su vez acredita el protocolo reverse engineered de `vivier/phomemo-tools`.

Importante: `iamjackg/esp32-phomemo-gameboy-printer` usa `BluetoothSerial`, es decir Bluetooth clasico/RFCOMM, no BLE GATT. El ESP32S3 no soporta Bluetooth clasico. Si tu T02 solo acepta RFCOMM, usa un ESP32 clasico como ESP32-WROOM-32 para el puente o cambia el objetivo de hardware.

Si tu T02 requiere UUIDs o comandos propietarios, ajusta:

- `SERVICE_UUID`
- `CHARACTERISTIC_UUID_TX`
- `CHARACTERISTIC_UUID_RX`
- `PRINTER_MAC_ADDRESS`
- `sendPhomemoRaster()`
