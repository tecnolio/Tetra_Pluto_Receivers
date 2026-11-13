# Procedencia y condiciones de terceros

Este ?rbol es una exportaci?n inicial de las fuentes del proyecto TETRA Pluto. Se mantienen avisos en cada archivo. No se atribuye una licencia ?nica a componentes con condiciones diferentes.

## HDL

- Proyecto base Fishball/PlutoSky R1 XC7Z020 derivado del [devkit Fishball](https://github.com/matsvandamme/fishball7020-fpga-devkit), recuperado del espacio de trabajo de referencia. Incluye dise?o de placa, BD, constraints y envoltorios necesarios, sin la instalaci?n de Vivado ni sus IP generadas.
- Fuentes ADI en `fpga/ip_repo/` y partes de `fpga/base/`: avisos Analog Devices preservados. Muchos m?dulos ofrecen GPLv2 o ADIBSD; lee cada encabezado. Se adjuntan los textos [GPL2](LICENSES/LICENSE_GPL2) y [ADIBSD](LICENSES/LICENSE_ADIBSD), obtenidos del [repositorio oficial ADI](https://github.com/analogdevicesinc/hdl). ADIBSD contiene condiciones de conexi?n a componentes ADI.
- Las IP de Xilinx/AMD se regeneran con tu instalaci?n/licencia de Vivado. No se redistribuyen herramientas ni archivos generados de sus IP.
- RTL de perfil, controladores y scripts propios proceden del proyecto de referencia. La publicaci?n conserva su fuente; cualquier aviso espec?fico prevalece.

## Voz: descarga separada, no incluida

| Dependencia | Commit fijado |
| --- | --- |
| [outerplane/tetra-codec](https://github.com/outerplane/tetra-codec) | `21d884064478d63306ec654378e666ae41503d00` |
| [curlyboi/libtetradec](https://github.com/curlyboi/libtetradec) | `62479b9ba33a086c94187bab62e102590bb33d75` |

Contienen o derivan de c?digo de referencia TETRA/ETSI. Revisa las condiciones originales y su alcance antes de compilar y, especialmente, antes de distribuir binarios. Que el c?digo est? accesible no supone una autorizaci?n universal para redistribuirlo. El script exige una aceptaci?n expl?cita de las condiciones upstream; no sustituye su lectura.

`backend/codec-include/` contiene adaptadores de compilaci?n del proyecto. No se incluyen los ?rboles de c?dec ni bibliotecas de sistema del equipo original. Se conserva tambi?n el texto GPLv3 que acompa?a al proyecto en `LICENSES/GPL-3.0.txt`; no cambia las condiciones particulares de otros componentes.

Los binarios y paquetes generados quedan en `build/`, excluidos de publicaci?n. La APK personal, claves y certificados de firma/TLS no forman parte de esta exportaci?n.
