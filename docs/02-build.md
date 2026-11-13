# 2. Compilaci?n reproducible por fuentes

## Herramientas

Entorno recomendado: Linux x86_64, Python ?3.10, Git, LLVM/Clang y LLD, Go ?1.20, Icarus Verilog y Vivado 2026.1 con dispositivo Zynq-7000 y licencia v?lida. Vivado/SDK no se redistribuyen. En Windows usa PowerShell para Python y un directorio local Linux/WSL para Vivado; evita compilar FPGA sobre UNC.

```sh
sudo apt-get update
sudo apt-get install git python3 python3-venv clang lld iverilog openssl
# Instala Go y Vivado desde sus distribuidores oficiales.
clang --version
ld.lld --version
go version
vivado -version
```

## Dependencias de audio

Lee [THIRD_PARTY.md](../THIRD_PARTY.md) y las condiciones de ambos upstream. La descarga est? fijada a commits y no incluye sus fuentes en este repositorio:

```sh
python3 tools/fetch_codecs.py --accept-upstream-terms
```

## Sysroot de la placa objetivo

El enlace usa las bibliotecas del sistema objetivo, no las de un PC. Se necesitan `libiio`, libc, libm, libpthread, libgcc_s y zlib. Verifica y registra su procedencia/versi?n; no mezcles ARM64, soft-float o una libc m?s nueva que la instalada.

Con SSH funcionando, verifica primero la huella del equipo y a??dela a `known_hosts` mediante el cliente SSH habitual. El capturador rechaza equipos desconocidos y ?nicamente lee bibliotecas:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install paramiko
python3 tools/capture_sysroot.py HOST_DEL_PLUTO --output private/sysroot
# Si no usas claves SSH: a?ade --password; se pide sin imprimirla.
python3 tools/build_backend.py --sysroot private/sysroot
```

Los resultados son `build/backend/pluto_tetra_profile`, `pluto_http` y `pluto_https`, para Linux ARMv7 hard-float. Se comprueba el encabezado ELF de los ejecutables C. Puedes indicar `--clang`, `--lld` y `--go` con rutas locales. Nada se copia al dispositivo.

## FPGA

El clon incluye el proyecto base y las cuatro IP ADI requeridas. No requiere el snapshot privado original.

```sh
python3 tools/validate.py
python3 tools/simulate.py
python3 -m unittest discover -s tests -v
mkdir -p build/fpga
vivado -mode batch -source fpga/build.tcl -log build/fpga/vivado.log -journal build/fpga/vivado.jou
python3 tools/convert_bitstream.py --directory build/fpga
```

La receta crea una copia en `build/fpga/vivado`, incorpora el RTL del perfil y sus GPIO, sintetiza y enruta. Rechaza slack negativo de setup/hold a 100 MHz. El conversor exige el marcador del build y el informe de timing correcto, verifica XC7Z020, invierte palabras de 32 bits para Linux y comprueba sincronismo.

Para repetir, conserva primero los informes y elimina ?nicamente `build/fpga/vivado` de este clon. No reutilices resultados antiguos como prueba de una compilaci?n nueva. No se ha completado todav?a este flujo Vivado desde el clon p?blico: [estado de validaci?n](VALIDATION.md).

```sh
python3 tools/package.py
sha256sum build/tetra-pluto-reference.tar.gz
```

El paquete contiene los ejecutables derivados, la FPGA y scripts; no credenciales. No se publica autom?ticamente. Antes de distribuir binarios revisa las condiciones de todas sus dependencias.
