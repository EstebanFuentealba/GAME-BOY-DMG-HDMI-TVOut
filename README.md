# GAME BOY DMG-001 → HDMI Output

<p align='center'>
    <picture>
        <img
            alt="Project"
            src="./assets/GB.png">
    </picture>
</p>

Hardware project to capture the LCD signal from an original Game Boy DMG-001 and output it to any HDMI monitor or TV using an RP2040 (Raspberry Pi Zero).

## How it works

<p align='center'>
    <picture>
        <img
            alt="Project"
            src="./assets/gameboy_dmg_hdmi_schema_v3.svg">
    </picture>
</p>

The Sharp LR35902 CPU generates raw LCD signals (VSYNC, HSYNC, Pixel Clock, D0–D1) at 5 V logic. These signals are level-shifted down to 3.3 V using individual BSS138 N-MOSFET transistors with 10 kΩ pull-up resistors on both sides. The RP2040 captures the shifted signals via its PIO state machines, buffers the 160×144 framebuffer in SRAM, and outputs a DVI/TMDS signal through a Micro-HDMI connector using a custom firmware.

## Hardware
- Game Boy DMG-001
- RP2040 (RP2040 Pi Zero)
- BSS138 N-MOSFETs (one per signal channel) + 10 kΩ resistors
- Micro-HDMI connector


## Pinout GAME BOY DMG-001

<p align='center'>
    <picture>
        <img
            alt="GB DMG Pinout Flex"
            src="./assets/GB-DMG-Pinout-Flex.png">
    </picture>
</p>

| Pin   | Nombre / Señal              | Descripción                                                                 |
|-------|----------------------------|-----------------------------------------------------------------------------|
| 01    | GND                        | Tierra                                                                      |
| 02    | Power LED                  | Voltaje de batería no regulado                                              |
| 03    | LCD Drive Voltage          | -19 V desde el convertidor de voltaje del CPU                               |
| 04    | Left & B Buttons           | Botones izquierda y B                                                       |
| 05    | Button Diodes 1 & 2        | Diodos de botones 1 y 2                                                     |
| 06    | Down & Start Buttons       | Botones abajo y Start                                                       |
| 07    | Up & Select Buttons        | Botones arriba y Select                                                     |
| 08    | Right & A Buttons          | Botones derecha y A                                                         |
| 09    | Button Diodes 3 & 4        | Diodos de botones 3 y 4                                                     |
| 10    | GND                        | Tierra                                                                      |
| 11    | Vcc                        | 5V regulado (distinto de Pin 02)                                            |
| 12    | VSync (?)                | Probablemente sincronización vertical (conecta a LCDV8)                     |
| 13    | ?                          | Conecta a LCDV6 y LCDH7 (DATALCH o ALTSIGL)                                 |
| 14    | CLK (?)                    | Posible clock (conecta a LCDH8)                                             |
| 15    | DATAOUT1 (?)               | Salida de datos 1 (conecta a LCDH9)                                         |
| 16    | DATAOUT0 (?)               | Salida de datos 0 (conecta a LCDH10)                                        |
| 17    | HSync                          | Conecta a LCDH11 (CONTROL o HORSYNC)                                        |
| 18    | ?                          | Conecta a LCDV10 y LCDH12 (DATALCH o ALTSIGL / posible CLK)                 |
| 19    | ?                          | Conecta a LCDH13                                                            |
| 20    | Speaker                    | Salida de audio para parlante                                               |
| 21    | GND                        | Tierra                


## DIY

- [RP2040 Pi Zero](https://s.click.aliexpress.com/e/_c3eepyXz)
- [Level shifter 5v - 3v3](https://s.click.aliexpress.com/e/_c3Bf8Tpz)
- [HDMI Mini to HDMI](https://s.click.aliexpress.com/e/_c2vEa6W7)
- [GBC Link Cable](https://s.click.aliexpress.com/e/_c3ozYTbh)
- [PortData EXT Link](https://s.click.aliexpress.com/e/_c4UrJ7iB)
- [GAME BOY DMG Shell Replacement](https://s.click.aliexpress.com/e/_c4pZA6Kj)
- [GAME BOY DMG Glass Light Gray](https://s.click.aliexpress.com/e/_c4oHB3KJ)
- [Audio Jack 3.5](https://s.click.aliexpress.com/e/_c2RgngHt)
