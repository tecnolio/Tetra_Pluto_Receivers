# Estado de validaci?n

Fecha: 2026-11-13.

## Realizado

- Recuperaci?n del proyecto base y dependencias HDL desde el archivo de desarrollo de referencia, preservando fuentes originales.
- Comprobaci?n de que todos los archivos de proyecto e IP ADI referenciados est?n incluidos.
- Compilaci?n del receptor y HTTP como ELF32 ARMv7 hard-float con LLVM/LLD y bibliotecas del sistema de referencia; proxy con Go para linux/arm/v7.
- Simulaci?n Icarus del canalizador: 12 casos (1, 2, 4 y 8 UL; mult?tono, UL aislado y DL aislado), todos con cero fallos.
- Cinco pruebas de herramientas: conversi?n de endianness, rechazo de parte incorrecta, truncamiento, falta de sincronismo y validaci?n de perfil/intervalo RF.
- Compilaci?n e instalaci?n de la APK personal 1.3 con enlace al repositorio; prueba DOM de navegaci?n, mapa, grupos y formularios superada contra API de solo lectura.

## Pendiente antes de una release instalable

- S?ntesis, implementaci?n y timing de una compilaci?n FPGA completa **desde este clon p?blico**. Un timing correcto de otro ?rbol no sirve como validaci?n de este.
- Instalaci?n desde cero en una placa nueva compatible, prueba de reinicio, exclusi?n del SDR y recuperaci?n de FPGA original.
- Verificar API, TLS, streaming, agenda y escucha real contra esta edici?n standalone; no se han desplegado estos cambios en el receptor de producci?n.
- Cliente Android configurable y publicaci?n Play. La APK personal mantiene su configuraci?n actual.

Los scripts de instalaci?n se publican para revisi?n y prueba guiada. No hay binarios FPGA p?blicos ni garant?a de portabilidad a placas diferentes.
