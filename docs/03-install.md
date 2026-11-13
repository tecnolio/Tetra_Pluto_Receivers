# 3. Instalar y poner en marcha

**Flujo inicial a?n pendiente de prueba completa en una placa nueva.** No ejecutarlo sobre un receptor en producci?n. El instalador rechaza instalaciones existentes y no modifica arranque, flash ni FPGA. Antes: [hardware](01-hardware.md), firmware del fabricante, copia de recuperaci?n y [build correcto](02-build.md).

## Identidad y HTTPS propios

Escoge tu hostname y puerto. Para acceso desde Android usa un certificado v?lido para ese hostname y una cadena que el cliente conf?e. La app personal actual incluye una CA concreta; un certificado nuevo no har? que esa APK se convierta en cliente gen?rico.

Prepara localmente `private/credentials/` con:

- `server.crt`: certificado y cadena del servidor.
- `server.key`: clave privada, permisos restringidos.
- `pluto-ca.crt`: CA p?blica correspondiente (para cadena p?blica, ra?z correspondiente).
- `web-auth`: **una l?nea base64 de `usuario:contrase?a`**, sin prefijo `Basic `. No es un hash: prot?gelo como una contrase?a.

Para generar `web-auth` sin dejar la contrase?a en el historial del shell:

```sh
mkdir -p private/credentials
python3 - <<'PY'
import base64, getpass, os
from pathlib import Path
user=input('Usuario: ')
if not user or ':' in user or any(ord(c)<32 for c in user): raise SystemExit('Usuario inv?lido')
password=getpass.getpass('Contrase?a: ')
if not password or any(ord(c)<32 for c in password): raise SystemExit('Contrase?a inv?lida')
p=Path('private/credentials/web-auth')
p.write_bytes(base64.b64encode((user+':'+password).encode())+b'\n')
os.chmod(p,0o600)
PY
```

Emite los certificados con tu proveedor habitual o tu CA y valida nombre, caducidad y cadena antes de conectar. No desactives la verificaci?n TLS. El backend HTTP queda en `127.0.0.1:8080`; externamente se publica solo HTTPS `:8443`. Una VPN evita exponer directamente la placa a Internet.

## Frecuencias y transferencia

Todas las frecuencias se expresan en **Hz**, no en MHz. Ejemplo sint?ctico; sustituye por tu perfil autorizado:

```sh
python3 tools/configure_network.py --ul 430000000 --dl 440000000 --uls 430000000,430025000 --name 'Mi red'
```

Esto solo genera archivos locales. No conecta ni sintoniza. Copia al equipo nuevo, por un transporte autenticado, el paquete `build/tetra-pluto-reference.tar.gz`, `private/config/`, `private/credentials/` y el **binario FPGA original compatible**. No presupongas que SFTP est? habilitado; con SSH y `cat` tambi?n puedes transferir archivos. Ejemplo desde Linux:

```sh
ssh root@HOST_DEL_PLUTO 'umask 077; mkdir -p /tmp/tetra-setup'
ssh root@HOST_DEL_PLUTO 'cat > /tmp/tetra-setup/package.tar.gz' < build/tetra-pluto-reference.tar.gz
```

Transfiere de la misma forma los dem?s archivos, manteniendo las credenciales en una carpeta privada. Verifica los SHA256 en ambos extremos. En el Pluto, con los archivos ya preparados:

```sh
cd /tmp/tetra-setup
tar xzf package.tar.gz
cd tetra-pluto-reference
sh runtime/preflight.sh
sh runtime/install.sh --new-target /tmp/tetra-setup/stock.bin /tmp/tetra-setup/credentials
cp /tmp/tetra-setup/config/tetra-networks.tsv /mnt/mmc/
cp /tmp/tetra-setup/config/tetra-active-pair.conf /mnt/mmc/
sh /mnt/jffs2/pluto_lab/start-web.sh
```

`stock.bin` debe ser el dise?o de f?brica para **esta** placa, no la FPGA de este proyecto. La instalaci?n preserva su copia en SD. El arranque prepara binarios y manifiestos en RAM pero deja la radio parada. Comprueba `/run/pluto_lab/runtime/http.log` y `https.log`, y abre `https://TU_HOST:8443/`.

## Activar y detener recepci?n expresamente

Los siguientes comandos **s? cargan la FPGA y toman el SDR**. Confirma antes que ning?n otro receptor/transmisor lo est? utilizando. `curl --user USUARIO` solicita la contrase?a sin incluirla en la l?nea de comandos. Para CA privada a?ade `--cacert /ruta/ca.crt`.

```sh
curl --fail --user USUARIO 'https://TU_HOST:8443/api/tetra/networks?action=list'
curl --fail --user USUARIO 'https://TU_HOST:8443/api/tetra/start'
curl --fail --user USUARIO 'https://TU_HOST:8443/api/tetra/fpga?action=status'
# Detener y restaurar la FPGA original (no reinicia el receptor de f?brica):
curl --fail --user USUARIO 'https://TU_HOST:8443/api/tetra/stop'
```

Revisa logs, reloj, estado FPGA y voz real antes de declarar operativo el equipo. La primera recepci?n requiere comprobar antena, ganancia, frecuencias y se?al. No se recupera voz cifrada ni se?al perdida.

## Arranque persistente y vuelta atr?s

Primero valida el inicio y parada manuales, incluida la restauraci?n stock. Despu?s integra `sh /mnt/jffs2/pluto_lab/start-web.sh` **una sola vez** en el hook de arranque documentado por el firmware del fabricante, cuando la SD y controladores est?n disponibles. Este repositorio no adivina ni sobrescribe `autorun.sh` o scripts del sistema. El hook solo debe iniciar API/TLS; la recepci?n sigue siendo una acci?n expl?cita.

Para volver atr?s: det?n la recepci?n mediante la API, confirma `operating` en FPGA y ausencia del proceso del receptor. Retira ?nicamente el hook a?adido. Conserva configuraci?n y copias antes de retirar las carpetas de la aplicaci?n. Si falla la restauraci?n, usa el procedimiento de recuperaci?n del fabricante con su imagen original; no fuerces cargas sucesivas sobre un SDR ocupado.
