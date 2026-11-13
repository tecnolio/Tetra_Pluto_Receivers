# 1. Equipo nuevo y sistema base

Este repositorio instala una aplicaci?n sobre un Linux compatible; no sustituye el firmware de arranque ni contiene una imagen de la flash de otro usuario.

## Comprobar antes de comprar o cargar una FPGA

Identifica fabricante, revisi?n de PCB, Zynq, transceptor, RAM, flash y m?todo de recuperaci?n. La referencia es Fishball/PlutoSky R1 XC7Z020 CLG400 -2 con AD9361. El proyecto base usa ese pinout y sus direcciones AXI; compartir el nombre ?Pluto? no basta.

Referencias del fabricante/comunidad: [Fishball devkit](https://github.com/matsvandamme/fishball7020-fpga-devkit) y [firmware 7020](https://github.com/Xiaozhang-code-cloud/Fish-Wan-plutosdr-fw-7020-SDR). El devkit contiene otras generaciones de sistema. Esta aplicaci?n conserva el contrato de Linux 5.15; una versi?n Debian/kernel m?s reciente requiere portar y probar los controladores y scripts.

Si llega sin sistema, instala **el firmware de su fabricante para su revisi?n exacta** siguiendo su procedimiento de recuperaci?n y conserva la imagen y documentaci?n. No se incluye aqu? un comando gen?rico para flashear: sin identificar la placa podr?a dejarla sin arranque.

## Inventario por SSH (solo lectura)

```sh
uname -a
cat /proc/cpuinfo
cat /proc/mounts
ls /sys/class/fpga_manager/fpga0
ls /sys/bus/iio/devices
ls /sys/bus/platform/drivers/cf_axi_adc
ls /sys/bus/platform/drivers/dma-axi-dmac
cat /sys/kernel/debug/clk/spi0.0-pl_intf_clk/clk_rate
```

En un equipo nuevo y sin propietario del SDR activo:

```sh
sh runtime/preflight.sh
```

Comprueba el chip, ARMv7, kernel y rutas de control; no carga la FPGA. Una comprobaci?n correcta **no certifica** el cableado ni una imagen de recuperaci?n. Si falla, no elimines las comprobaciones para forzar la carga.

Prepara una SD fiable montada en `/mnt/mmc`, SSH y una IP estable. Guarda fuera del dispositivo el firmware original, el ?rbol de dispositivos y **el binario FPGA original para Linux FPGA manager de esa misma placa y firmware**. Se necesita para restaurar el dise?o de f?brica al detener la recepci?n. Un archivo `.bit` de Vivado no equivale a ese `.bin`.

No uses im?genes completas del equipo original: pueden contener claves, MAC, certificados y configuraci?n personal.
