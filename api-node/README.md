# API Node.js

API con Express y un módulo de conexión a Firestore. Por ahora solo tiene un endpoint de prueba.

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
| `FIREBASE_PROJECT_ID` | ID del proyecto de Firebase. | Valor `project_id` de la cuenta de servicio. |
| `FIREBASE_CLIENT_EMAIL` | Correo de la cuenta de servicio. | Valor `client_email`. |
| `FIREBASE_PRIVATE_KEY` | Clave privada de la cuenta de servicio. | Valor `private_key`, entre comillas dobles. |

`HOST` y `PORT` son obligatorias para el servidor local. Las variables de Firebase son necesarias al usar Firestore. `/api/health` puede funcionar sin ellas.

Las variables se leen desde `.env`, que no se sube al repositorio. En Vercel se configurarán en el proyecto y se usará la aplicación exportada en `app.js`.

## Configurar Firestore

1. Crear o abrir el proyecto en la [consola de Firebase](https://console.firebase.google.com/).
2. Crear la base de datos Firestore `(default)` en modo producción.
3. En la configuración del proyecto, abrir **Cuentas de servicio** y generar una clave privada. Guardar el JSON fuera del repositorio.
4. Copiar `project_id`, `client_email` y `private_key` a las variables correspondientes de `.env`. Si el archivo ya existe, agregar las variables sin sobrescribirlo.

Copiar la clave privada entre comillas dobles, conservando los `\n` del JSON. `firestore.js` los convierte en saltos de línea al iniciar Firebase Admin.

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

## Referencias

- [Datos y endpoints propuestos](../Docs/Contratos/API-Node.md).
- [Ejemplo básico de Express](https://expressjs.com/en/starter/hello-world/), de Express y OpenJS Foundation: referencia para crear la aplicación y sus rutas.
- [Express en Vercel](https://vercel.com/docs/frameworks/backend/express): referencia para exportar la aplicación.
- [Variables de entorno en Node.js](https://nodejs.org/docs/latest-v24.x/api/cli.html#--env-filefile): carga de `.env`.
- [Firebase Admin SDK](https://firebase.google.com/docs/admin/setup): configuración de la cuenta de servicio y las credenciales.
- [Firestore desde un servidor](https://firebase.google.com/docs/firestore/quickstart-server): inicialización del cliente y acceso a la base de datos.
- [Subir claves de cuentas de servicio](https://docs.cloud.google.com/iam/docs/keys-upload): registro del certificado público en Google Cloud.
