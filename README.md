# 240213 ESP-NOW naar S88

## Doel

ESP32-prototype dat S88-modulewoorden via ESP-NOW ontvangt en ze als een virtuele S88-keten aanbiedt. De firmware heeft daarnaast een OTA-modus die bij het opstarten via de bootknop wordt gekozen.

## Werking volgens `src/main.cpp`

- Houdt zes 16-bits S88-woorden bij.
- De ESP-NOW-ontvangst verwerkt pakketten op basis van `ID`, `OFFSET`, `AANTAL` en `s88Data`.
- De S88-klokinterrupt schuift de ontvangen bits uit op de S88-data-uitgang.
- De OLED toont de ontvangen bitpatronen; seriële debug meldt ontvangen pakketgegevens.
- In OTA-modus verbindt de ESP32 met wifi en verwerkt `ArduinoOTA`-updates.

## Hardware en bouwen

- PlatformIO-target: `esp32doit-devkit-v1`
- S88-clock: GPIO 23
- S88-load: GPIO 18
- S88-data-uitgang: GPIO 19
- OLED I2C: SDA GPIO 22, SCL GPIO 21
- Seriële debug: 115200 baud
- Voor wifi/OTA-gebruik: vervang de wifi- en OTA-password-placeholders in `src/main.cpp` en `src/meen.cpp`.
- PlatformIO bouwt standaard alleen `src/main.cpp`; `src/meen.cpp` is een alternatieve firmwarevariant en bevat eigen `setup()`/`loop()`-functies.

Bouwen vanuit deze projectmap:

```sh
pio run
```

## Protocol- en veiligheidsnotities

Deze firmware hoort bij de oudere packetvariant met `uint16_t`-modulewoorden, niet bij de bytepayload van `240518S88Host2ESPNow`. De code kopieert ontvangen data naar een volledige struct zonder eerst `len` tegen de verwachte pakketgrootte te controleren. Controleer en verbeter die validatie voordat je deze prototypefirmware met onbekende zenders gebruikt. De wifi- en OTA-instellingen zijn voor publicatie vervangen door placeholders.
