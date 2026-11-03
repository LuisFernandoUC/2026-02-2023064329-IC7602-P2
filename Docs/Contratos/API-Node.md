# API Node.js: datos y endpoints

**Responsable:** Nicole Parra Valverde

**Estado:** implementados `/api/health`, `GET /api/domains`, `POST /api/domains`, la administración de reglas y `GET /api/cache/config`. Las demás rutas son propuestas.

Basado en las páginas 4 y 5 del [enunciado](../Proyectos-2026-02-IC7602-P2.pdf).

## 1. Comunicación

```text
UI -> API Node.js -> Firestore
Caché zonal -> API Node.js -> Firestore
```

La UI usa la API para administrar dominios, URLs, usuarios y claves. La caché la usa para consultar la configuración y validar el acceso.

La caché en C guarda y sirve los archivos, controla el tamaño y aplica los algoritmos de reemplazo.

## 2. Datos mínimos

Campos propuestos. Tamaños en bytes y TTL en segundos.

| Dato | Campos mínimos | Uso |
| --- | --- | --- |
| Cuenta del panel | `id` (UID de Firebase Authentication), `email` | Propietario de los dominios. |
| Dominio | `id`, `ownerId`, `name`, `originUrl`, `maxCacheBytes`, `verified`, `verificationToken` | Dominio, propietario, servidor de origen, límite de caché y verificación TXT. |
| Regla de URL | `id`, `pathPattern`, `maxCacheBytes`, `fileTypes`, `authMode` | Configuración de una ruta, guardada bajo su dominio y con soporte de comodines. |
| Usuario de un recurso | `id`, `ruleId`, `username`, `passwordHash` | Acceso a URLs con usuario y contraseña. |
| API key | `id`, `ruleId`, `keyHash` | Acceso a URLs con `x-api-key`. |
| Sesión de un recurso | `id`, `ruleId`, `userId`, `tokenHash`, `expiresAt` | Acceso después del login, hasta que venza la sesión. |

`fileTypes` es una lista de tipos de archivo. Cada uno tiene `extension`, `ttlSeconds` y `replacementPolicy`. Las políticas son `LRU`, `LFU`, `FIFO`, `MRU` y `Random`.

`authMode` puede ser `api-key` o `password`. Los usuarios de recursos son distintos de la cuenta del panel.

Las contraseñas de usuarios de recursos, las claves y los tokens de sesión se guardarán como hashes. Los hashes de contraseñas no se enviarán a la UI ni a las cachés. Los hashes SHA-256 de las API keys se entregan solo a las cachés autenticadas para que validen `x-api-key`. Una API key se mostrará solo al crearla. Firebase Authentication maneja las contraseñas de las cuentas del panel.

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
| DELETE | `/api/domains/:domainId` | Eliminar el dominio, sus reglas, usuarios, claves y sesiones. |
| POST | `/api/domains/:domainId/verify` | Consultar el TXT y verificar la propiedad. |
| GET | `/api/domains/:domainId/rules` | Listar las reglas de URL. Implementada. |
| POST | `/api/domains/:domainId/rules` | Crear una regla de URL. Implementada. |
| PUT | `/api/domains/:domainId/rules/:ruleId` | Modificar una regla de URL. Implementada. |
| DELETE | `/api/domains/:domainId/rules/:ruleId` | Eliminar la regla y sus subcolecciones. Implementada. |
| GET | `/api/rules/:ruleId/users` | Listar usuarios de acceso, sin contraseñas ni hashes. |
| POST | `/api/rules/:ruleId/users` | Crear un usuario de acceso. |
| PUT | `/api/rules/:ruleId/users/:userId` | Modificar el usuario o su contraseña. |
| DELETE | `/api/rules/:ruleId/users/:userId` | Eliminar el usuario y cerrar sus sesiones. |
| GET | `/api/rules/:ruleId/keys` | Listar los identificadores de las API keys. |
| POST | `/api/rules/:ruleId/keys` | Crear una API key y devolver su valor una sola vez. |
| DELETE | `/api/rules/:ruleId/keys/:keyId` | Eliminar una API key. |

Al agregar un dominio, la API devuelve el registro `_verificacion.<dominio>` con un token generado en el servidor. Se guarda con `verified: false`. La consulta del TXT para verificar la propiedad sigue pendiente. Solo los dominios verificados se enviarán a las cachés.

`originUrl` es obligatorio al agregar un dominio. Debe ser una dirección HTTP o HTTPS, con puerto opcional, sin rutas, credenciales, parámetros ni fragmentos. Se guarda su dirección base, por ejemplo `http://apache-apache:8080`, para que la caché añada la ruta del recurso. Los dominios anteriores no se modifican automáticamente.

El listado filtra por el UID de la sesión y el registro toma ese mismo UID como `ownerId`. El cliente no puede elegir otro propietario ni marcar el dominio como verificado. El mismo dominio no puede repetirse en una cuenta.

Las reglas se guardan en `domains/{domainId}/rules/{ruleId}`. Todas sus operaciones comprueban la sesión y el propietario del dominio. Se pueden configurar mientras el dominio esté pendiente de verificación. El tamaño debe ser positivo y no superar el límite del dominio; las extensiones no se repiten, el TTL es un entero mayor o igual que cero y las políticas son las cinco indicadas arriba. El ID se genera en el servidor.

El listado responde `200`, el registro `201`, la edición `200` y la eliminación `204`. Datos inválidos responden `400`; un dominio ajeno o inexistente, o una regla inexistente, responde `404`. Un fallo de Firestore responde `503`. La edición conserva el ID y las subcolecciones; la eliminación usa `recursiveDelete` para no dejarlas huérfanas.

La comprobación del propietario se agregará al implementar las rutas restantes del panel.

## 4. Endpoints para las cachés

El endpoint de configuración sigue el formato que ya acepta la caché, descrito en [Integración](../Integracion.md). Las rutas de sesiones siguen propuestas.

| Método | Ruta | Operación y acceso |
| --- | --- | --- |
| GET | `/api/cache/config` | Entregar todos los dominios verificados, sus reglas y hashes de API keys. Requiere el token de la caché. Implementada. |
| POST | `/api/cache/sessions` | Recibir dominio, ruta, usuario y contraseña desde el formulario de la UI y crear una sesión. |
| DELETE | `/api/cache/sessions/current` | Cerrar la sesión del token recibido. |

La configuración tiene la forma `{ "domains": [...] }`. Cada dominio incluye `name`, `originUrl` si está guardado, `maxCacheBytes` y `rules`. Cada regla incluye su `id`, patrón, tamaño, mecanismo de acceso, tipos de archivo y `apiKeys` con los hashes SHA-256. No incluye propietarios, tokens TXT, usuarios ni hashes de contraseñas.

La caché envía `Authorization: Bearer <CACHE_API_TOKEN>`. Ese valor se configura como `ZC_CONFIG_TOKEN` en la caché y es distinto de la API key del cliente. La respuesta usa `Cache-Control: no-store`. Sin dominios verificados devuelve `{ "domains": [] }`; sin token válido responde `401` y si falta la configuración del token o falla Firestore responde `503`.

La caché valida las API keys con los hashes recibidos, sin consultar a la API en cada petición. El login y las sesiones por recurso quedan pendientes.

## 5. Pendientes

| Acuerdo | Participantes |
| --- | --- |
| Aplicar los permisos por propietario al implementar las demás rutas del panel. | Nicole |
| Implementar la consulta del registro TXT para verificar dominios. | Nicole y Luis Fernando |
| Definir cómo funciona `*`, qué regla gana si coinciden varias y qué pasa si ninguna coincide. | Nicole y Fabricio |
| Definir el formato de configuración, el origen de los archivos y cómo la caché recibe los cambios, incluidas las eliminaciones de claves o usuarios. | Nicole y Fabricio |
| Configurar el mismo valor de `CACHE_API_TOKEN` y `ZC_CONFIG_TOKEN` al conectar la caché con la API. | Nicole y Fabricio |
| Definir el flujo de login: formulario, entrega del token, uso, vencimiento, cierre de sesión y regreso al recurso. | Nicole y Fabricio |
| Definir qué datos necesita el DNS Interceptor para reconocer los dominios verificados. | Nicole y Luis Fernando |

La configuración de los servicios y sus credenciales se leerá de variables de entorno.
