# API Node.js

API con Express, registro y listado de dominios en Firestore, autenticación con Firebase Authentication y consulta de configuración para las cachés.

Publicada en [zonal-cache-api.vercel.app](https://zonal-cache-api.vercel.app). La comprobación de salud está en [`/api/health`](https://zonal-cache-api.vercel.app/api/health).

## Ejecutar

Se necesita Node.js 24 y npm. Desde la raíz del repositorio:

```bash
cd api-node
npm ci
cp -n .env.example .env
npm run dev
```

`cp -n` conserva `.env` si ya existe. Para ejecutar sin recarga automática, usar `npm start`. Para detener el servidor, presionar `Ctrl+C`.

## Variables de entorno

| Variable | Uso | Ejemplo |
| --- | --- | --- |
| `HOST` | Dirección donde escucha el servidor local. | `127.0.0.1` |
| `PORT` | Puerto del servidor local, de 1 a 65535. | `3000` |
| `UI_ORIGIN` | Origen de la UI permitido por CORS. | `http://127.0.0.1:5173` |
| `FIREBASE_PROJECT_ID` | ID del proyecto de Firebase. | Valor `project_id` de la cuenta de servicio. |
| `FIREBASE_CLIENT_EMAIL` | Correo de la cuenta de servicio. | Valor `client_email`. |
| `FIREBASE_PRIVATE_KEY` | Clave privada de la cuenta de servicio. | Valor `private_key`, entre comillas dobles. |
| `CACHE_API_TOKEN` | Token para consultar `/api/cache/config`. | Mismo valor que `ZC_CONFIG_TOKEN` en la caché. |
| `CACHE_SESSION_SECRET` | Secreto de al menos 32 bytes para firmar las sesiones de recursos. | Mismo valor que `ZC_SESSION_SECRET` en la caché. |
| `CACHE_SESSION_SECONDS` | Duración de la sesión del recurso en segundos; entero positivo. | `3600` |

`HOST` y `PORT` son obligatorias para el servidor local. Las variables de Firebase se usan para Firestore y para validar sesiones. `/api/health` puede funcionar sin ellas. `UI_ORIGIN` debe coincidir con la dirección de la UI, incluido el puerto.

Las variables se leen desde `.env`, que no se sube al repositorio. En Vercel están configuradas en el proyecto con raíz `api-node` y se usa la aplicación exportada en `app.js`. El origen permitido allí es `UI_ORIGIN=https://zonal-cache-ui.vercel.app`.

## Configurar Firestore

1. Crear o abrir el proyecto en la [consola de Firebase](https://console.firebase.google.com/).
2. Crear la base de datos Firestore `(default)` en modo producción.
3. En la configuración del proyecto, abrir **Cuentas de servicio** y generar una clave privada. Guardar el JSON fuera del repositorio.
4. Copiar `project_id`, `client_email` y `private_key` a las variables correspondientes de `.env`. Si el archivo ya existe, agregar las variables sin sobrescribirlo.

Copiar la clave privada entre comillas dobles, conservando los `\n` del JSON. `firebase.js` los convierte en saltos de línea al iniciar Firebase Admin.

`getDb()` devuelve el cliente de Firestore y reutiliza la misma aplicación de Firebase en las siguientes llamadas. La clave privada se usa solo en el servidor.

Para comprobar el acceso:

```bash
npm run check:firestore
```

El comando consulta hasta un documento de `domains`. No modifica ni muestra sus datos; funciona aunque la colección esté vacía. Si tiene acceso, imprime `Conexión con Firestore verificada.`. Si falla, muestra el error y termina con código distinto de cero.

Esta comprobación necesita credenciales reales y la base de datos creada. Inicializar el cliente por sí solo no confirma el acceso a Firestore.

El proyecto del grupo es `IC7602-P2`, con ID `ic7602-p2-e9950`. Usa el plan Spark y Firestore Standard, base `(default)`, ubicación `nam5` y modo producción.

La clave actual se generó en el equipo y se registró su certificado público en Google Cloud. La clave privada está en `.env`, con una copia fuera del repositorio. Vence el 21/10/2027.

Se comprobó el acceso con `npm run check:firestore`. Resultado: `Conexión con Firestore verificada.`

## Autenticación del panel

Se usa Firebase Authentication con correo y contraseña porque el proyecto ya usa Firebase y permite manejar las cuentas y la sesión sin guardar contraseñas en la API.

La UI registra e inicia sesión con el SDK de Firebase. En sus solicitudes a la API envía el ID token en `Authorization: Bearer <token>`. `requireAuth` lo verifica con Firebase Admin y obtiene el UID de la cuenta. Ese UID identifica al propietario de los dominios.

El listado y el registro de dominios usan `requireAuth`, que responde `401` si falta el token o no es válido, y `503` si falla la validación por un problema del servicio o de configuración. La autenticación de usuarios de recursos usa el endpoint separado `/api/cache/sessions`.

## Dominios

`GET /api/domains` devuelve una lista con los dominios de la cuenta autenticada. Si no tiene dominios, devuelve `[]`.

`POST /api/domains` recibe el nombre, el servidor de origen y el límite de caché en bytes:

```json
{"name":"apache.local","originUrl":"http://apache-apache","maxCacheBytes":1048576}
```

El nombre se guarda en minúsculas. El propietario se obtiene de la sesión y el dominio queda sin verificar, con un token para el registro TXT `_verificacion.<dominio>`. La respuesta incluye el dominio y los datos de ese registro.

`originUrl` es obligatorio y acepta HTTP o HTTPS con puerto opcional. No admite rutas, credenciales, parámetros ni fragmentos. La API guarda la dirección base sin la barra final; la caché añade la ruta solicitada. Puede ser un nombre interno como `http://apache-apache` si la caché puede resolverlo. La API no intenta conectarse al origen al guardar.

El listado devuelve el origen guardado. Los dominios creados antes de este campo se conservan y la UI los muestra con el origen **Sin configurar**.

Responde `201` al guardar, `400` si los datos no son válidos, `409` si el dominio ya está en esa cuenta y `503` si falla Firestore. El límite debe ser un entero positivo de bytes y el nombre debe permitir formar un registro TXT de hasta 253 caracteres.

`POST /api/domains/:domainId/verify` consulta `_verificacion.<dominio>` con el DNS del servidor. Une los fragmentos de cada registro TXT y los compara con el token guardado. Si coincide, guarda `verified: true` y devuelve el dominio actualizado. No acepta un token ni un estado de verificación enviados por el cliente. Si el dominio ya está verificado, devuelve su estado actual.

La verificación responde `409` si el TXT no aparece, no coincide o el nombre ya está verificado en otra cuenta. Los errores de DNS o Firestore responden `503`. `apache.local` sirve para probar el guardado local; verificar la propiedad requiere poder modificar el DNS que consulta la API.

`DELETE /api/domains/:domainId` elimina el dominio y todas sus subcolecciones mediante `recursiveDelete`. Responde `204` al terminar y `503` si falla el borrado. Verificar y eliminar comprueban la sesión y el propietario; un dominio ajeno o inexistente responde `404`.

## Reglas de URL

Las rutas `/api/domains/:domainId/rules` requieren la sesión del panel y comprueban que el dominio pertenece a la cuenta:

- `GET` lista las reglas.
- `POST` crea una regla y devuelve su ID.
- `PUT /:ruleId` guarda los cambios de una regla existente.
- `DELETE /:ruleId` elimina la regla y sus subcolecciones.

El cuerpo de creación o edición usa bytes y segundos:

```json
{"pathPattern":"/prueba/*","maxCacheBytes":524288,"authMode":"api-key","fileTypes":[{"extension":"html","ttlSeconds":60,"replacementPolicy":"LRU"}]}
```

El patrón empieza con `/` y puede incluir comodines. El tamaño es un entero positivo que no supera el límite del dominio. Debe haber al menos una extensión, sin repeticiones, con TTL entero mayor o igual que cero y política `LRU`, `LFU`, `FIFO`, `MRU` o `Random`. La extensión `*` aplica a los tipos sin fila propia. Se acepta `none` (ruta pública), `api-key` o `password` como mecanismo de acceso; una regla `none` no admite claves ni usuarios (`409`).

Las reglas se guardan en `domains/{domainId}/rules/{ruleId}` y pueden configurarse antes de verificar el dominio. Una edición conserva su ID y las subcolecciones. Una eliminación usa `recursiveDelete`; no se puede deshacer desde la UI.

Respuestas: `200` al listar o editar, `201` al crear, `204` al eliminar, `400` para datos inválidos, `404` para un dominio ajeno o datos inexistentes y `503` si falla Firestore. Ninguna operación permite elegir otro propietario.

La API conserva la política de cada tipo de archivo y la caché aplica cada una por separado. Los tipos comparten el límite de tamaño de la regla.

## API keys

Las rutas `/api/domains/:domainId/rules/:ruleId/keys` comprueban la sesión, el propietario del dominio y la existencia de la regla:

- `GET` lista solo los identificadores de las claves.
- `POST` genera una clave y devuelve `{ id, apiKey }` con estado `201`.
- `DELETE /:keyId` elimina la clave y responde `204`.

La creación usa 24 bytes aleatorios en base64url y guarda únicamente su hash SHA-256 en la subcolección `keys` de la regla. El valor original se entrega solo al crearla. La regla debe estar guardada con `authMode: "api-key"`; si no, la creación responde `409`. Las respuestas usan `Cache-Control: no-store`. Una clave eliminada deja de funcionar cuando la caché vuelve a consultar su configuración.

## Usuarios de recursos

Las rutas `/api/domains/:domainId/rules/:ruleId/users` usan los mismos permisos del dominio y la regla:

- `GET` lista los usuarios como `{ id, username }`.
- `POST` recibe `{ username, password }` y crea un usuario con estado `201`.
- `PUT /:userId` cambia el nombre o la contraseña y responde `200`. Una contraseña vacía u omitida conserva la anterior.
- `DELETE /:userId` elimina al usuario y responde `204`.

Crear y editar requiere que la regla tenga `authMode: "password"`. El nombre se recorta y se consulta para rechazar duplicados en esa regla con `409`; el mismo nombre puede usarse en otra regla. La API guarda solo el nombre y `passwordHash` en la subcolección `users`. Usa scrypt con una sal aleatoria de 16 bytes y un hash de 64 bytes, en el formato acordado en `Docs/Integracion.md`. No devuelve contraseñas ni hashes al panel o a la caché. Las respuestas usan `Cache-Control: no-store`.

Estos usuarios son distintos de las cuentas de Firebase Authentication.

## Login de recursos

`POST /api/cache/sessions` es público y recibe `{ domain, path, username, password, callback }` desde `#/acceso-recurso`. No necesita una cuenta del panel ni el token de configuración de la caché.

Busca el dominio verificado y la regla más específica para la ruta. Usa los comodines `*` y `?`, y gana el patrón con más caracteres fijos, igual que la caché. La regla debe usar acceso por contraseña. El usuario se busca solo dentro de esa regla y su contraseña se verifica con scrypt.

El callback debe ser HTTP o HTTPS, del mismo dominio y con ruta `/__zc/auth`, sin credenciales ni fragmentos. Su parámetro `next` debe volver a la ruta solicitada; si falta, se usa esa ruta. Puede conservar los parámetros de consulta del recurso.

Al validar responde `201` con `{ redirect, expiresAt }`. La dirección incluye el JWT HS256 con `domain`, `sub`, `rule` y `exp`. Las sesiones no se guardan en Firestore. Las respuestas usan `Cache-Control: no-store`; los datos inválidos responden `400`, el acceso denegado `401` y los fallos del servicio o de configuración `503`.

Configurar en `.env`:

- `CACHE_SESSION_SECRET`: secreto de al menos 32 bytes. Generarlo con `openssl rand -hex 32` y usar el mismo valor como `ZC_SESSION_SECRET` en la caché.
- `CACHE_SESSION_SECONDS`: entero positivo con la duración en segundos, por ejemplo `3600`.

La UI vuelve a la caché con el token y la caché guarda su cookie. `/__zc/logout` borra esa cookie. Un token emitido sigue siendo válido hasta que venza; eliminar un usuario impide nuevos logins. Las eliminaciones de reglas o dominios llegan a la caché cuando actualiza su configuración.

## Configuración para las cachés

`GET /api/cache/config` requiere `Authorization: Bearer <CACHE_API_TOKEN>`. Este token es propio de la caché; no usa la sesión de una cuenta del panel ni la API key de un cliente.

Devuelve `{ "domains": [...] }` con todos los dominios que tienen `verified: true`. Lee las reglas en `domains/{domainId}/rules` y, para las reglas con `authMode: "api-key"`, sus hashes en `rules/{ruleId}/keys`. Entrega los nombres de campos que ya acepta la caché: `originUrl`, `maxCacheBytes`, `pathPattern`, `authMode`, `fileTypes` y `apiKeys`.

La respuesta no incluye propietarios, tokens TXT, usuarios, contraseñas ni hashes de contraseñas. Usa `Cache-Control: no-store` y vuelve a consultar Firestore en cada solicitud. Si falla alguna lectura, responde `503` sin entregar datos parciales.

Si no hay dominios verificados, devuelve `200` y `{ "domains": [] }`. Un token ausente o incorrecto responde `401`. Si falta `CACHE_API_TOKEN`, responde `503`.

Se comprobó la lectura desde una ejecución local de la caché contra la API de Vercel. `proxycache.duckdns.org` ya está verificado por TXT y aparece con su regla en la configuración. Falta completar la prueba con la caché desplegada y Apache.

Agregar `CACHE_API_TOKEN` a `.env` y usar el mismo valor como `ZC_CONFIG_TOKEN` en la caché. Se puede generar un valor con `openssl rand -hex 32`. No subirlo al repositorio ni ponerlo en variables `VITE_`.

## Probar

Con el servidor activo, abrir otra terminal:

```bash
curl -i http://127.0.0.1:3000/api/health
```

Debe responder con HTTP `200` y este JSON:

```json
{"status":"ok"}
```

Una ruta inexistente responde con HTTP `404` y `{"error":"Ruta no encontrada"}`. Si se cambia `HOST` o `PORT`, ajustar la dirección del comando.

Para probar la autenticación, los dominios, las reglas, las API keys, los usuarios de recursos y la configuración de las cachés:

```bash
npm test
```

Las pruebas cubren autenticación, validaciones, duplicados, separación de cuentas y errores del servicio. Las de configuración verifican el token de caché, el filtro de dominios verificados, los datos entregados, las claves revocadas y los fallos de Firestore. Usan credenciales locales y documentos en memoria. No crean cuentas ni modifican Firebase.

Las pruebas de reglas cubren el guardado y la consulta, la edición, la eliminación de subcolecciones, los permisos por propietario, los datos inválidos y la entrega de una regla guardada a `/api/cache/config`.

Las de API keys comprueban la creación, el listado sin valores ni hashes, la eliminación, los permisos y los fallos de Firestore. También verifican que la configuración de la caché incluya el hash guardado y deje de entregarlo al eliminar la clave.

Las de usuarios comprueban el hash de la contraseña, el listado sin datos secretos, la edición con y sin cambio de contraseña, los duplicados por regla, la eliminación, los permisos y los fallos de Firestore.

Las de dominios incluyen la verificación TXT con registros en fragmentos, los errores de DNS, los permisos y el borrado de subcolecciones. Las de sesiones comprueban la firma y el alcance del JWT, el vencimiento configurado, la selección de reglas, las credenciales, los callbacks y los fallos del servicio. DNS y Firestore se simulan en estas pruebas; no verifican ni borran dominios reales.

Con la API en ejecución, consultar el endpoint usando el valor de `CACHE_API_TOKEN` de `.env`:

```bash
curl -i -H 'Authorization: Bearer <CACHE_API_TOKEN>' http://127.0.0.1:3000/api/cache/config
```

Reemplazar `<CACHE_API_TOKEN>` por el valor local. Debe responder `200` con la lista de dominios verificados, o una lista vacía si aún no hay ninguno. Repetir sin el encabezado debe responder `401`.

## Comprobar API y UI

Ejecutar desde esta carpeta, con las URLs publicadas:

```bash
API_URL=https://zonal-cache-api.vercel.app UI_URL=https://zonal-cache-ui.vercel.app npm run check:deployment
```

Usa `CACHE_API_TOKEN` del `.env` local. Comprueba disponibilidad, CORS, Firestore y rechazo de accesos inválidos, sin modificar datos. Si se omiten las URLs, usa las direcciones locales.

El 08/11/2026 se comprobaron estas respuestas en Vercel: salud y UI `200`, CORS permitido para la UI, dominios y configuración sin autorización `401`, configuración autenticada `200` con `Cache-Control: no-store`, y login con datos inválidos `401`.

También se probaron las operaciones del panel con Firestore real y la verificación TXT con DuckDNS. Los pasos y resultados están en [las pruebas del README principal](../README.md#92-pruebas-de-api-y-ui-en-vercel). Sigue pendiente el login válido y el recorrido completo por la caché desplegada hasta Apache.

## Referencias

- [Datos y endpoints](../Docs/Contratos/API-Node.md).
- [Ejemplo básico de Express](https://expressjs.com/en/starter/hello-world/), de Express y OpenJS Foundation: referencia para crear la aplicación y sus rutas.
- [Express en Vercel](https://vercel.com/docs/frameworks/backend/express): referencia para exportar la aplicación.
- [Variables de entorno en Node.js](https://nodejs.org/docs/latest-v24.x/api/cli.html#--env-filefile): carga de `.env`.
- [Firebase Admin SDK](https://firebase.google.com/docs/admin/setup): configuración de la cuenta de servicio y las credenciales.
- [Verificar ID tokens](https://firebase.google.com/docs/auth/admin/verify-id-tokens): validación de la sesión en la API con Firebase Admin.
- [Firestore desde un servidor](https://firebase.google.com/docs/firestore/quickstart-server): inicialización del cliente y acceso a la base de datos.
- [Crear documentos](https://docs.cloud.google.com/nodejs/docs/reference/firestore/latest/firestore/documentreference#create_data_): creación de documentos sin sobrescribir los existentes.
- [Consultas de Firestore](https://firebase.google.com/docs/firestore/query-data/queries): filtrado de dominios por propietario.
- [Lectura de datos de Firestore](https://firebase.google.com/docs/firestore/query-data/get-data): lectura de reglas y claves en subcolecciones.
- [Escritura de datos de Firestore](https://firebase.google.com/docs/firestore/manage-data/add-data): creación y actualización de reglas.
- [Borrado recursivo en Firestore](https://googleapis.dev/nodejs/firestore/latest/Firestore.html#recursiveDelete): eliminación de una regla y sus subcolecciones.
- [Comparación de tokens en Node.js](https://nodejs.org/docs/latest-v24.x/api/crypto.html#cryptotimingsafeequala-b): uso de `timingSafeEqual`.
- [scrypt en Node.js](https://nodejs.org/docs/latest-v24.x/api/crypto.html#cryptoscryptpassword-salt-keylen-options-callback): hash de contraseñas con sal aleatoria.
- [Consultas TXT en Node.js](https://nodejs.org/docs/latest-v24.x/api/dns.html#dnspromisesresolvetxthostname): lectura de registros DNS y sus fragmentos.
- [JSON Web Token (RFC 7519)](https://www.rfc-editor.org/rfc/rfc7519.html): formato del token de sesión.
- [Subir claves de cuentas de servicio](https://docs.cloud.google.com/iam/docs/keys-upload): registro del certificado público en Google Cloud.
