# API Node.js: datos y endpoints

**Responsable:** Nicole Parra Valverde

**Estado:** implementados `/api/health`, la administración de dominios, reglas, API keys y usuarios de recursos, `GET /api/cache/config` y `POST /api/cache/sessions`.

Basado en las páginas 4 y 5 del [enunciado](../Proyectos-2026-02-IC7602-P2.pdf).

## 1. Comunicación

```text
UI -> API Node.js -> Firestore
Caché zonal -> API Node.js -> Firestore
```

La UI usa la API para administrar dominios, URLs, usuarios y claves. La caché la usa para consultar la configuración y validar el acceso.

La caché en C guarda y sirve los archivos, controla el tamaño y aplica los algoritmos de reemplazo.

## 2. Datos mínimos

Tamaños en bytes y TTL en segundos.

| Dato | Campos mínimos | Uso |
| --- | --- | --- |
| Cuenta del panel | `id` (UID de Firebase Authentication), `email` | Propietario de los dominios. |
| Dominio | `id`, `ownerId`, `name`, `originUrl`, `maxCacheBytes`, `verified`, `verificationToken` | Dominio, propietario, servidor de origen, límite de caché y verificación TXT. |
| Regla de URL | `id`, `pathPattern`, `maxCacheBytes`, `fileTypes`, `authMode` | Configuración de una ruta, guardada bajo su dominio y con soporte de comodines. |
| Usuario de un recurso | `id`, `ruleId`, `username`, `passwordHash` | Acceso a URLs con usuario y contraseña. |
| API key | `id`, `ruleId`, `keyHash` | Acceso a URLs con `x-api-key`. |
| Sesión de un recurso | JWT con `domain`, `sub`, `rule`, `exp` | Acceso al dominio y regla indicados hasta que venza. No se guarda en Firestore. |

`fileTypes` es una lista de tipos de archivo. Cada uno tiene `extension`, `ttlSeconds` y `replacementPolicy`. Las políticas son `LRU`, `LFU`, `FIFO`, `MRU` y `Random`.

`authMode` puede ser `api-key` o `password`. Los usuarios de recursos son distintos de la cuenta del panel.

Las contraseñas de usuarios de recursos y las API keys se guardan como hashes. Los hashes de contraseñas no se envían a la UI ni a las cachés. Los hashes SHA-256 de las API keys se entregan solo a las cachés autenticadas para que validen `x-api-key`. Una API key se muestra solo al crearla. Las sesiones de recursos usan JWT firmados con el secreto compartido con la caché. Firebase Authentication maneja las cuentas del panel.

### Ejemplo de regla de URL

```json
{
  "id": "regla-1",
  "pathPattern": "/imagenes/*",
  "maxCacheBytes": 10485760,
  "fileTypes": [
    {
      "extension": "png",
      "ttlSeconds": 3600,
      "replacementPolicy": "LRU"
    }
  ],
  "authMode": "api-key"
}
```

La suma de archivos en caché no puede superar el límite del dominio, aunque cada regla tenga su propio límite.

## 3. Endpoints para el panel

Estas rutas requieren iniciar sesión. Cada cuenta solo puede administrar sus propios datos.

El registro, inicio y cierre de sesión del panel usan Firebase Authentication con correo y contraseña desde la UI. Las solicitudes envían el ID token en `Authorization: Bearer <token>`. Las rutas de listado y registro de dominios usan `requireAuth` para verificarlo con Firebase Admin y obtener el propietario del UID verificado.

| Método | Ruta | Operación |
| --- | --- | --- |
| GET | `/api/domains` | Listar los dominios de la cuenta. Implementada. |
| POST | `/api/domains` | Agregar un dominio, su servidor de origen y límite de caché, y generar el valor TXT. Implementada. |
| DELETE | `/api/domains/:domainId` | Eliminar el dominio y sus subcolecciones. Implementada. |
| POST | `/api/domains/:domainId/verify` | Consultar el TXT y verificar la propiedad. Implementada. |
| GET | `/api/domains/:domainId/rules` | Listar las reglas de URL. Implementada. |
| POST | `/api/domains/:domainId/rules` | Crear una regla de URL. Implementada. |
| PUT | `/api/domains/:domainId/rules/:ruleId` | Modificar una regla de URL. Implementada. |
| DELETE | `/api/domains/:domainId/rules/:ruleId` | Eliminar la regla y sus subcolecciones. Implementada. |
| GET | `/api/domains/:domainId/rules/:ruleId/users` | Listar usuarios de acceso, sin contraseñas ni hashes. Implementada. |
| POST | `/api/domains/:domainId/rules/:ruleId/users` | Crear un usuario de acceso. Implementada. |
| PUT | `/api/domains/:domainId/rules/:ruleId/users/:userId` | Modificar el usuario o su contraseña. Implementada. |
| DELETE | `/api/domains/:domainId/rules/:ruleId/users/:userId` | Eliminar un usuario de acceso. Implementada. |
| GET | `/api/domains/:domainId/rules/:ruleId/keys` | Listar los identificadores de las API keys. Implementada. |
| POST | `/api/domains/:domainId/rules/:ruleId/keys` | Crear una API key y devolver su valor una sola vez. Implementada. |
| DELETE | `/api/domains/:domainId/rules/:ruleId/keys/:keyId` | Eliminar una API key. Implementada. |

Al agregar un dominio, la API devuelve el registro `_verificacion.<dominio>` con un token generado en el servidor. Se guarda con `verified: false`. La verificación consulta ese TXT y solo marca `verified: true` si un registro completo coincide con el token guardado. Solo los dominios verificados se envían a las cachés.

Verificar y eliminar comprueban la sesión y el propietario. La verificación responde `200` con el dominio actualizado, `409` si falta el TXT, no coincide o el nombre ya está verificado en otra cuenta, y `503` ante fallos del DNS o Firestore. Repetir una verificación exitosa devuelve el dominio sin cambiarlo. La eliminación usa `recursiveDelete`, responde `204` y borra también las reglas, usuarios y claves. Un dominio ajeno o inexistente responde `404`.

`originUrl` es obligatorio al agregar un dominio. Debe ser una dirección HTTP o HTTPS, con puerto opcional, sin rutas, credenciales, parámetros ni fragmentos. Se guarda su dirección base, por ejemplo `http://apache-apache:8080`, para que la caché añada la ruta del recurso. Los dominios anteriores no se modifican automáticamente.

El listado filtra por el UID de la sesión y el registro toma ese mismo UID como `ownerId`. El cliente no puede elegir otro propietario ni marcar el dominio como verificado. El mismo dominio no puede repetirse en una cuenta.

Las reglas se guardan en `domains/{domainId}/rules/{ruleId}`. Todas sus operaciones comprueban la sesión y el propietario del dominio. Se pueden configurar mientras el dominio esté pendiente de verificación. El tamaño debe ser positivo y no superar el límite del dominio; las extensiones no se repiten, el TTL es un entero mayor o igual que cero y las políticas son las cinco indicadas arriba. El ID se genera en el servidor.

El listado responde `200`, el registro `201`, la edición `200` y la eliminación `204`. Datos inválidos responden `400`; un dominio ajeno o inexistente, o una regla inexistente, responde `404`. Un fallo de Firestore responde `503`. La edición conserva el ID y las subcolecciones; la eliminación usa `recursiveDelete` para no dejarlas huérfanas.

Las API keys usan los mismos permisos del dominio. Se generan en el servidor y solo se guarda `keyHash` en `domains/{domainId}/rules/{ruleId}/keys/{keyId}`. La creación responde `201` con `{ id, apiKey }`; el listado devuelve únicamente los IDs y la eliminación responde `204`. Crear una clave requiere que la regla esté guardada con `authMode: "api-key"`; de lo contrario responde `409`. Las respuestas usan `Cache-Control: no-store`.

Los usuarios se guardan en `domains/{domainId}/rules/{ruleId}/users/{userId}`, con los mismos permisos. Crear o editar requiere que la regla esté guardada con `authMode: "password"`. El cuerpo recibe `username` y `password`; al editar, omitir la contraseña o dejarla vacía conserva la actual. El nombre se recorta y se comprueba que no esté repetido en esa regla; un duplicado responde `409`. La contraseña se guarda como `scrypt$<sal hexadecimal>$<hash hexadecimal>`, con una sal aleatoria por contraseña. Las respuestas solo incluyen `id` y `username`: `201` al crear, `200` al listar o editar y `204` al eliminar.

## 4. Endpoints para las cachés

Los endpoints siguen el formato que ya acepta la caché, descrito en [Integración](../Integracion.md).

| Método | Ruta | Operación y acceso |
| --- | --- | --- |
| GET | `/api/cache/config` | Entregar todos los dominios verificados, sus reglas y hashes de API keys. Requiere el token de la caché. Implementada. |
| POST | `/api/cache/sessions` | Validar las credenciales del recurso y devolver la dirección de regreso con un JWT firmado. Implementada. No requiere la sesión del panel. |

La configuración tiene la forma `{ "domains": [...] }`. Cada dominio incluye `name`, `originUrl` si está guardado, `maxCacheBytes` y `rules`. Cada regla incluye su `id`, patrón, tamaño, mecanismo de acceso, tipos de archivo y `apiKeys` con los hashes SHA-256. No incluye propietarios, tokens TXT, usuarios ni hashes de contraseñas.

La caché envía `Authorization: Bearer <CACHE_API_TOKEN>`. Ese valor se configura como `ZC_CONFIG_TOKEN` en la caché y es distinto de la API key del cliente. La respuesta usa `Cache-Control: no-store`. Sin dominios verificados devuelve `{ "domains": [] }`; sin token válido responde `401` y si falta la configuración del token o falla Firestore responde `503`.

La caché valida las API keys con los hashes recibidos, sin consultar a la API en cada petición.

El login recibe `{ domain, path, username, password, callback }`. Busca el dominio verificado y la regla más específica para la ruta, con el mismo criterio de comodines que la caché. La regla debe usar `password`. Valida la contraseña con scrypt y responde `201` con `{ redirect, expiresAt }`. `redirect` apunta a `/__zc/auth` del mismo dominio, con la ruta de regreso y el JWT HS256. La sesión incluye el dominio, el nombre del usuario, el ID de la regla y el vencimiento. Requiere `CACHE_SESSION_SECRET` y `CACHE_SESSION_SECONDS`.

Los datos o callbacks inválidos responden `400`; las credenciales incorrectas, un dominio no verificado o una regla sin acceso por contraseña responden `401`; los fallos del servicio o la configuración responden `503`. La respuesta usa `Cache-Control: no-store`. El callback debe ser HTTP o HTTPS, del mismo dominio, sin credenciales ni fragmento. La ruta de regreso debe coincidir con el recurso solicitado.

La caché guarda el JWT en su cookie. El cierre usa `/__zc/logout`, que borra esa cookie; no requiere otra ruta en la API. Un token ya emitido sigue siendo válido hasta su vencimiento. Eliminar un usuario impide nuevos logins; los cambios de reglas y dominios llegan cuando la caché vuelve a consultar su configuración.

## 5. Pendientes

| Acuerdo | Participantes |
| --- | --- |
| Confirmar en el clúster el mismo token de configuración y secreto de sesión que usa la API publicada. La lectura desde la caché local ya se comprobó. | Nicole y Fabricio |
| Probar el login válido, Apache con MISS/HIT y el cierre de sesión con la caché desplegada. | Nicole y Fabricio, con acceso al entorno de Luis Fernando |
| Integrar y probar los cambios de `feat/dns-interceptor` que consultan los dominios verificados en `/api/cache/config`. | Nicole y Luis Fernando |

El formato de configuración, el origen de los archivos y la actualización de cambios ya están implementados como se describe arriba. La configuración de los servicios y sus credenciales se lee de variables de entorno.
