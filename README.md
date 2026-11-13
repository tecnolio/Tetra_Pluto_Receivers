# Tetra Pluto Receivers

Receptor TETRA para Pluto basado en FPGA, API con perfiles UL/DL y escucha de grupos. Guía en español para preparar un equipo nuevo y compilar las fuentes sin depender del entorno privado original.

**Estado: versión inicial de referencia, no instalador universal.** El software ARM se compila con esta receta. Falta ejecutar una compilación FPGA completa desde este clon y validar la instalación desde cero en una placa nueva. No hay una imagen de firmware lista para flashear. La APK Android configurable se ofrece como versión de prueba.

## Descargar la app Android

[**Descargar Pluto Pocket 1.4 (.apk)**](https://github.com/tecnolio/Tetra_Pluto_Receivers/raw/refs/heads/main/downloads/PlutoPocket-1.4.apk) · [SHA256](downloads/PlutoPocket-1.4.apk.sha256)

Android 8 o posterior. **Sin URL de Pluto preconfigurada**: introduces URL/IP, puerto y credenciales al conectar. Conserva GitHub y los mapas. Versión de prueba, todavía pendiente de validación física de 1.4; [instrucciones y límites](downloads/README.md).

## Hardware y límites actuales

| Componente | Referencia soportada por este árbol |
| --- | --- |
| Placa | Fishball / PlutoSky R1, Zynq **XC7Z020 CLG400 -2**, AD9361 |
| Sistema | Linux **5.15**, ARMv7 hard-float, libiio y rutas de control compatibles |
| FPGA | Vivado **2026.1**, proyecto y dependencias HDL incluidos |
| Recepción | 1–8 frecuencias UL; DL principal y canal auxiliar según el receptor |
| Perfil RF | DL = UL + **10 MHz**, pasos de **25 kHz**, intervalo total ≤ **11,8 MHz** |
| Muestreo / reloj | 12,5 MS/s; interfaz RX como máximo 100 MHz |

Un ADALM-Pluto original XC7Z010 u otra placa, revisión, kernel o separación dúplex necesita adaptaci?n. **No cargues esta FPGA en él.** Los perfiles no convierten automáticamente cualquier red TETRA en compatible.

## Empieza aquí

1. [Identificar la placa y preparar el sistema base](docs/01-hardware.md).
2. [Compilar software ARM y FPGA](docs/02-build.md).
3. [Instalar en una placa nueva, configurar red y arrancar](docs/03-install.md).
4. [API, Android y futura distribución](docs/04-app-api.md).
5. [Comprobaciones y límites de validaci?n](docs/VALIDATION.md).

```sh
git clone https://github.com/tecnolio/Tetra_Pluto_Receivers.git
cd Tetra_Pluto_Receivers
python3 tools/validate.py
python3 -m unittest discover -s tests -v
```

`backend/` contiene el receptor, servidor HTTP y proxy HTTPS. `fpga/` incluye RTL, banco de pruebas, proyecto base e IP de ADI. `runtime/` incluye control, instalación y preparaci?n en RAM. Compilar no instala ni inicia radio.

No se publican credenciales, claves, certificados personales, agendas SSI, capturas, SDK privados, códecs de terceros ni binarios precompilados. Consulta [procedencia y licencias](THIRD_PARTY.md) antes de redistribuir derivados.
