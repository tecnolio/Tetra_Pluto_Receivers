# Descargar Pluto Pocket

[**Descargar Pluto Pocket 1.4 para Android**](https://github.com/tecnolio/Tetra_Pluto_Receivers/raw/refs/heads/main/downloads/PlutoPocket-1.4.apk)

Android 8 o posterior (API 26). Versión de prueba firmada; no está publicada en Google Play. No incluye URL de Pluto, credenciales ni CA personal preconfiguradas.

1. Instala el APK e introduce la URL/IP HTTPS, puerto, usuario y contraseña de tu servidor.
2. Si tu servidor utiliza una CA privada, selecciona su certificado público mediante «Importar CA propia».
3. Consulta [la guía de conexión](../docs/04-app-api.md) y [la preparación del receptor](../README.md).

GitHub y OpenStreetMap mantienen sus enlaces. Cambiar la conexión detiene la escucha y descarta los datos del servidor anterior. La aplicación permite consultar mapa, terminales, agenda, grupos y audio; la edición de frecuencias UL/DL desde Android sigue pendiente.

Esta versión ha pasado compilación, auditoría del APK, 17 casos de URL/puerto y nueve pruebas de interfaz/audio/configuración. La validación física de 1.4 sigue pendiente.

## Integridad

[Archivo SHA256](PlutoPocket-1.4.apk.sha256)

```sh
sha256sum -c PlutoPocket-1.4.apk.sha256
```

Las claves de firma y las credenciales no se publican. La licencia de Leaflet está incluida en el APK como `assets/LEAFLET-LICENSE.txt`; el cliente reproduce PCM del servidor y no incorpora sus códecs TETRA.
