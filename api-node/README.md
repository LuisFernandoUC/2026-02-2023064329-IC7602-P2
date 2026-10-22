# API Node.js

Base de la API con Express. Por ahora solo tiene un endpoint de prueba. Falta conectar Firestore.

## Ejecutar

Se necesita Node.js 24 y npm. Desde la raíz del repositorio:

```bash
cd api-node
npm ci
cp .env.example .env
npm run dev
```

Para ejecutar sin recarga automática, usar `npm start`. Para detener el servidor, presionar `Ctrl+C`.

## Variables de entorno

| Variable | Uso | Ejemplo |
| --- | --- | --- |
| `HOST` | Dirección donde escucha el servidor local. | `127.0.0.1` |
| `PORT` | Puerto del servidor local, de 1 a 65535. | `3000` |

Ambas variables son obligatorias para el servidor local. Se leen desde `.env`, que no se sube al repositorio. En Vercel se usará la aplicación exportada en `app.js`.

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
