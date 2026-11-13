# 4. API y Android

## Qu? existe y qu? falta

La API de este árbol acepta perfiles compartidos, guarda nombres SSI y grupos y proporciona posiciones y audio.

La APK 1.4 de desarrollo **no incluye una URL de Pluto preconfigurada**. Al conectar pide URL/IP, puerto y credenciales; datos y audio usan ese servidor. Conserva el enlace de ayuda a este repositorio y los mapas de OpenStreetMap. El acceso se guarda cifrado y los cambios de conexión descartan datos y peticiones anteriores.

Usa HTTPS con validación del nombre del servidor y certificados de Android. Si usas una CA privada, selecciona su archivo público en «Importar CA propia» dentro del diálogo de conexión; no se incluye la CA personal del equipo de referencia. Al actualizar desde la APK anterior se configura el servidor una vez.

**Pendiente:** editar/aplicar perfiles UL/DL desde Android, validar la versión configurable en dispositivos físicos y publicar una APK genérica distribuible. Este repositorio no incluye todavía esa APK ni anuncia disponibilidad en Google Play. El servidor necesita API, FPGA y controladores compatibles.

## Contrato del servidor

HTTPS, autenticaci?n Basic sobre TLS, UTF-8 y JSON. Las credenciales de la app deben mantenerse en Android Keystore; no incluirlas en URLs, logs o enlaces. Las frecuencias son enteros en Hz.

| Operaci?n | Ruta |
| --- | --- |
| Perfiles y estado de aplicaci?n | `GET /api/tetra/networks?action=list` |
| Guardar perfil | `POST /api/tetra/networks?action=save&id=...&name=...&ul=...&dl=...&uls=...&dls=...` |
| Aplicar perfil y activar recepci?n | `POST /api/tetra/networks?action=apply&id=...` |
| Eliminar perfil | `POST /api/tetra/networks?action=delete&id=...` |
| Arranque/parada expl?citos | `GET /api/tetra/start`, `GET /api/tetra/stop` |
| Estado FPGA | `GET /api/tetra/fpga?action=status` |
| Estado / posiciones | `GET /api/status`, `GET /api/positions` |
| Agenda / grupos / llamadas | `GET /api/tetra/names`, `/api/tetra/groups`, `/api/tetra/calls` |
| Audio | `GET /api/tetra/audio` con los par?metros de grupo del cliente |

Los par?metros POST se env?an **en la query**, no como JSON en el cuerpo. Codifica nombres y listas con percent-encoding. `uls` y `dls` son listas separadas por comas; incluye las frecuencias principales. La lista puede guardar datos v?lidos a nivel de cat?logo que excedan el perfil FPGA: el controlador RF aplica l?mites adicionales al iniciar.

Guardar no sintoniza; **aplicar s? puede detener el perfil anterior y arrancar otro**. El cliente debe mostrar la acci?n y esperar a `apply_status`, tratando 409 como conflicto/SDR ocupado. No mostrar ?xito solo porque el servidor responde que ha aceptado el trabajo. La agenda es compartida; una edici?n desde Android alimenta la agenda del servidor al que se ha conectado, no una base global.

## Preparaci?n para Play

El futuro cliente debe ofrecer configuraci?n por servidor, TLS verificable, pantalla adaptativa m?vil/tablet, servicio de audio en primer plano, controles de bloqueo, permisos de ubicaci?n solicitados con contexto y pol?tica de privacidad. Requiere firma de publicaci?n estable y revisar las condiciones de distribuci?n de sus dependencias. La instalaci?n actual en un m?vil y el c?digo de servidor no equivalen a una aprobaci?n de Play.
