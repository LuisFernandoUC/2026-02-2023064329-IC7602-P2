# API Node.js: datos y endpoints

**Responsable:** Nicole Parra Valverde

**Estado:** borrador, sin implementar.

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
| Cuenta del panel | `id`, `email` | Propietario de los dominios. |
| Dominio | `id`, `ownerId`, `name`, `maxCacheBytes`, `verified`, `verificationToken` | Dominio, propietario, límite de caché y verificación TXT. |
| Regla de URL | `id`, `domainId`, `pathPattern`, `maxCacheBytes`, `fileTypes`, `authMode` | Configuración de una ruta, con soporte de comodines. |
| Usuario de un recurso | `id`, `ruleId`, `username`, `passwordHash` | Acceso a URLs con usuario y contraseña. |
| API key | `id`, `ruleId`, `keyHash` | Acceso a URLs con `x-api-key`. |
| Sesión de un recurso | `id`, `ruleId`, `userId`, `tokenHash`, `expiresAt` | Acceso después del login, hasta que venza la sesión. |

`fileTypes` es una lista de tipos de archivo. Cada uno tiene `extension`, `ttlSeconds` y `replacementPolicy`. Las políticas son `LRU`, `LFU`, `FIFO`, `MRU` y `Random`.

`authMode` puede ser `api-key` o `password`. Los usuarios de recursos son distintos de la cuenta del panel.

Las contraseñas, claves y tokens de sesión se guardarán como hashes. Los hashes no se enviarán a la UI ni a las cachés. Una API key se mostrará solo al crearla.

### Ejemplo de regla de URL

```json
{
  "id": "regla-1",
  "domainId": "dominio-1",
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

| Método | Ruta | Operación |
| --- | --- | --- |
| GET | `/api/domains` | Listar los dominios de la cuenta. |
| POST | `/api/domains` | Agregar un dominio, su límite de caché y generar el valor TXT. |
| DELETE | `/api/domains/:domainId` | Eliminar el dominio, sus reglas, usuarios, claves y sesiones. |
| POST | `/api/domains/:domainId/verify` | Consultar el TXT y verificar la propiedad. |
| GET | `/api/domains/:domainId/rules` | Listar las reglas de URL. |
| POST | `/api/domains/:domainId/rules` | Crear una regla de URL. |
| PUT | `/api/domains/:domainId/rules/:ruleId` | Modificar una regla de URL. |
| DELETE | `/api/domains/:domainId/rules/:ruleId` | Eliminar la regla, sus usuarios, claves y sesiones. |
| GET | `/api/rules/:ruleId/users` | Listar usuarios de acceso, sin contraseñas ni hashes. |
| POST | `/api/rules/:ruleId/users` | Crear un usuario de acceso. |
| PUT | `/api/rules/:ruleId/users/:userId` | Modificar el usuario o su contraseña. |
| DELETE | `/api/rules/:ruleId/users/:userId` | Eliminar el usuario y cerrar sus sesiones. |
| GET | `/api/rules/:ruleId/keys` | Listar los identificadores de las API keys. |
| POST | `/api/rules/:ruleId/keys` | Crear una API key y devolver su valor una sola vez. |
| DELETE | `/api/rules/:ruleId/keys/:keyId` | Eliminar una API key. |

Al agregar un dominio, la API devuelve el nombre y el valor del TXT que se debe crear. Luego consulta ese registro para verificar la propiedad. Solo los dominios verificados se envían a las cachés.

Falta elegir cómo se hará el registro, inicio y cierre de sesión del panel. Las rutas se definirán según esa elección.

## 4. Endpoints para las cachés

Rutas propuestas. Falta revisarlas con Fabricio.

| Método | Ruta | Operación y acceso |
| --- | --- | --- |
| GET | `/api/cache/config?domain=example.com` | Consultar el límite del dominio y sus reglas, sin credenciales ni hashes. Requiere autenticar la caché. |
| POST | `/api/cache/authorize` | Validar una API key o sesión para un dominio y una ruta. Requiere autenticar la caché. |
| POST | `/api/cache/sessions` | Recibir dominio, ruta, usuario y contraseña desde el formulario de la UI y crear una sesión. |
| DELETE | `/api/cache/sessions/current` | Cerrar la sesión del token recibido. |

La API busca la regla que corresponde al dominio y la ruta. La sesión solo sirve para ese recurso y deja de ser válida cuando vence o se cierra.

La caché debe autenticarse ante la API. Esa autenticación es distinta de la API key del cliente. Falta definir cómo se enviará cada una.

## 5. Pendientes

| Acuerdo | Participantes |
| --- | --- |
| Elegir la autenticación del panel y los permisos de la API. | Nicole |
| Definir el nombre y el valor del registro TXT. | Nicole y Luis Fernando |
| Definir cómo funciona `*`, qué regla gana si coinciden varias y qué pasa si ninguna coincide. | Nicole y Fabricio |
| Definir el formato de configuración, el origen de los archivos y cómo la caché recibe los cambios, incluidas las eliminaciones de claves o usuarios. | Nicole y Fabricio |
| Definir cómo se autentica la caché y qué envía y recibe al validar el acceso. | Nicole y Fabricio |
| Definir el flujo de login: formulario, entrega del token, uso, vencimiento, cierre de sesión y regreso al recurso. | Nicole y Fabricio |
| Definir qué datos necesita el DNS Interceptor para reconocer los dominios verificados. | Nicole y Luis Fernando |

La configuración de los servicios y sus credenciales se leerá de variables de entorno.
