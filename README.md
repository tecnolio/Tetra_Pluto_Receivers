# Tetra Pluto Receivers

Receptor TETRA para Pluto basado en FPGA, API con perfiles UL/DL y escucha de grupos. Gu?a en espa?ol para preparar un equipo nuevo y compilar las fuentes sin depender del entorno privado original.

**Estado: versi?n inicial de referencia, no instalador universal.** El software ARM se compila con esta receta. Falta ejecutar una compilaci?n FPGA completa desde este clon y validar la instalaci?n desde cero en una placa nueva. No hay una imagen de firmware lista para flashear ni una APK p?blica configurable todav?a.

## Hardware y l?mites actuales

| Componente | Referencia soportada por este ?rbol |
| --- | --- |
| Placa | Fishball / PlutoSky R1, Zynq **XC7Z020 CLG400 -2**, AD9361 |
| Sistema | Linux **5.15**, ARMv7 hard-float, libiio y rutas de control compatibles |
| FPGA | Vivado **2026.1**, proyecto y dependencias HDL incluidos |
| Recepci?n | 1?8 frecuencias UL; DL principal y canal auxiliar seg?n el receptor |
| Perfil RF | DL = UL + **10 MHz**, pasos de **25 kHz**, intervalo total ? **11,8 MHz** |
| Muestreo / reloj | 12,5 MS/s; interfaz RX como m?ximo 100 MHz |

Un ADALM-Pluto original XC7Z010 u otra placa, revisi?n, kernel o separaci?n d?plex necesita adaptaci?n. **No cargues esta FPGA en ?l.** Los perfiles no convierten autom?ticamente cualquier red TETRA en compatible.

## Empieza aqu?

1. [Identificar la placa y preparar el sistema base](docs/01-hardware.md).
2. [Compilar software ARM y FPGA](docs/02-build.md).
3. [Instalar en una placa nueva, configurar red y arrancar](docs/03-install.md).
4. [API, Android y futura distribuci?n](docs/04-app-api.md).
5. [Comprobaciones y l?mites de validaci?n](docs/VALIDATION.md).

```sh
git clone https://github.com/tecnolio/Tetra_Pluto_Receivers.git
cd Tetra_Pluto_Receivers
python3 tools/validate.py
python3 -m unittest discover -s tests -v
```

`backend/` contiene el receptor, servidor HTTP y proxy HTTPS. `fpga/` incluye RTL, banco de pruebas, proyecto base e IP de ADI. `runtime/` incluye control, instalaci?n y preparaci?n en RAM. Compilar no instala ni inicia radio.

No se publican credenciales, claves, certificados personales, agendas SSI, capturas, SDK privados, c?decs de terceros ni binarios precompilados. Consulta [procedencia y licencias](THIRD_PARTY.md) antes de redistribuir derivados.
