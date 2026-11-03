# API Node.js

API con Express, registro y listado de dominios en Firestore y autenticación con Firebase Authentication.

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

`HOST` y `PORT` son obligatorias para el servidor local. Las variables de Firebase se usan para Firestore y para validar sesiones. `/api/health` puede funcionar sin ellas. `UI_ORIGIN` debe coincidir con la dirección de la UI, incluido el puerto.

Las variables se leen desde `.env`, que no se sube al repositorio. En Vercel se configurarán en el proyecto y se usará la aplicación exportada en `app.js`.

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

El listado y el registro de dominios usan `requireAuth`, que responde `401` si falta el token o no es válido, y `503` si falla la validación por un problema del servicio o de configuración. La autenticación de usuarios de recursos se hará por separado.

## Dominios

`GET /api/domains` devuelve una lista con los dominios de la cuenta autenticada. Si no tiene dominios, devuelve `[]`.

`POST /api/domains` recibe el nombre y el límite de caché en bytes:

```json
{"name":"apache.local","maxCacheBytes":1048576}
```

El nombre se guarda en minúsculas. El propietario se obtiene de la sesión y el dominio queda sin verificar, con un token para el registro TXT `_verificacion.<dominio>`. La respuesta incluye el dominio y los datos de ese registro.

Responde `201` al guardar, `400` si los datos no son válidos, `409` si el dominio ya está en esa cuenta y `503` si falla Firestore. El límite debe ser un entero positivo de bytes y el nombre debe permitir formar un registro TXT de hasta 253 caracteres.

La eliminación de dominios, la consulta del TXT, las reglas y las credenciales siguen pendientes. `apache.local` sirve para probar el guardado local; verificar la propiedad requiere poder modificar el DNS que consulta la API.

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

Para probar la autenticación y los dominios:

```bash
npm test
```

Las pruebas cubren autenticación, validaciones, duplicados, separación de cuentas y errores del servicio. Usan credenciales locales y documentos en memoria. No crean cuentas ni modifican Firebase.

## Referencias

- [Datos y endpoints propuestos](../Docs/Contratos/API-Node.md).
- [Ejemplo básico de Express](https://expressjs.com/en/starter/hello-world/), de Express y OpenJS Foundation: referencia para crear la aplicación y sus rutas.
- [Express en Vercel](https://vercel.com/docs/frameworks/backend/express): referencia para exportar la aplicación.
- [Variables de entorno en Node.js](https://nodejs.org/docs/latest-v24.x/api/cli.html#--env-filefile): carga de `.env`.
- [Firebase Admin SDK](https://firebase.google.com/docs/admin/setup): configuración de la cuenta de servicio y las credenciales.
- [Verificar ID tokens](https://firebase.google.com/docs/auth/admin/verify-id-tokens): validación de la sesión en la API con Firebase Admin.
- [Firestore desde un servidor](https://firebase.google.com/docs/firestore/quickstart-server): inicialización del cliente y acceso a la base de datos.
- [Crear documentos](https://docs.cloud.google.com/nodejs/docs/reference/firestore/latest/firestore/documentreference#create_data_): creación de documentos sin sobrescribir los existentes.
- [Consultas de Firestore](https://firebase.google.com/docs/firestore/query-data/queries): filtrado de dominios por propietario.
- [Subir claves de cuentas de servicio](https://docs.cloud.google.com/iam/docs/keys-upload): registro del certificado público en Google Cloud.
