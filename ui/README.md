# UI

Panel con React, Vite y Tailwind CSS. El registro, inicio y cierre de sesión usan Firebase Authentication. Los dominios, las reglas de URL y las API keys están conectados a la API y Firestore. Los usuarios de recursos siguen pendientes.

## Ejecutar

Se necesita Node.js 24 y npm. Desde la raíz del repositorio:

```bash
cd ui
npm ci
cp -n .env.example .env
npm run dev -- --host 127.0.0.1
```

Antes de iniciar, completar `.env`. `cp -n` conserva el archivo si ya existe. La API se ejecuta por separado siguiendo [sus instrucciones](../api-node/README.md).

## Configurar Firebase

En la consola del proyecto, habilitar **Authentication → Método de acceso → Correo electrónico/contraseña**. En **Configuración del proyecto → General**, registrar una app web y copiar su configuración a estas variables:

| Variable | Valor |
| --- | --- |
| `VITE_API_URL` | Dirección de la API, por ejemplo `http://127.0.0.1:3000`. |
| `VITE_FIREBASE_API_KEY` | `apiKey` de la app web. |
| `VITE_FIREBASE_AUTH_DOMAIN` | `authDomain`. |
| `VITE_FIREBASE_PROJECT_ID` | `projectId`. |
| `VITE_FIREBASE_APP_ID` | `appId`. |

En `IC7602-P2` ya está registrada la app web **Proxy cache** y está habilitado el acceso con correo y contraseña. No hace falta crear otra app.

Estas variables son la configuración pública del SDK web. La clave privada de la cuenta de servicio va solo en la API. `.env` está excluido de Git. Reiniciar Vite después de cambiarlo.

## Acceso

Abrir `http://127.0.0.1:5173/`. **Crear cuenta** registra el correo y la contraseña en Firebase e inicia sesión. Una cuenta existente entra desde **Iniciar sesión**. Firebase conserva la sesión al recargar y **Cerrar sesión** la termina en el navegador.

Las pantallas del panel requieren sesión. Las solicitudes hechas con `apiRequest` envían el ID token para que la API lo verifique. El listado muestra únicamente los dominios de esa cuenta y empieza vacío.

**Agregar dominio** guarda el nombre, el servidor de origen y el límite de caché. El origen debe ser una dirección HTTP o HTTPS, con puerto opcional y sin rutas, como `http://apache-apache` para el Apache del cluster. La caché debe poder acceder a ese servidor. La UI muestra MB y la API recibe bytes (1 MB = 1 048 576 bytes). Al guardarlo aparece como pendiente y se pueden consultar los datos de su registro TXT. La verificación y la eliminación todavía no están disponibles.

El detalle del dominio muestra su servidor de origen. Si fue creado antes de agregar este campo, muestra **Sin configurar**; no se cambia su configuración automáticamente.

Desde el detalle se pueden agregar, editar y eliminar reglas de URL. El formulario guarda el patrón, el tamaño de caché, el mecanismo de acceso y los tipos de archivo con su TTL y política. Convierte MB a bytes al guardar. Después vuelve a la lista y consulta las reglas guardadas. Para eliminar una regla se pide confirmación.

En **Editar regla**, si el acceso guardado es por API key, se pueden crear y eliminar claves. Una clave nueva se muestra una sola vez para copiarla; **Ocultar clave** retira su valor de la pantalla. La lista muestra solo los identificadores. La eliminación pide confirmación y llega a las cachés cuando actualizan su configuración. Si se cambia el mecanismo de acceso, hay que guardar la regla antes de administrar sus claves o usuarios. Los usuarios de recursos siguen pendientes.

`#/acceso-recurso` es el formulario para usuarios de recursos y permanece público. Todavía falta conectarlo con la API de acceso a las cachés; no usa la cuenta del panel.

## Comprobar

```bash
npm run build
```

Para probar el flujo completo, crear una cuenta desde la UI, recargar la página, cerrar sesión e ingresar de nuevo. Sin sesión, abrir `#/dominios` debe volver a **Iniciar sesión**. Contraseñas distintas en el registro deben mostrar un mensaje.

Para probar dominios, agregar `apache.local` con origen `http://apache-apache` y un límite de 1 MB. Abrirlo y recargar: debe conservar el origen y seguir pendiente. Intentar agregarlo otra vez debe mostrar un error. Si ya existe, no se debe volver a crear. Este nombre se usa para las pruebas locales del grupo; no confirma propiedad mediante DNS público.

Para probar reglas en ese dominio, agregar `/prueba/*` con 0.5 MB, tipo `html`, TTL de 60 segundos, política `LRU` y acceso por API key. Guardar, recargar y abrir **Editar**: los datos deben conservarse. Cambiar el TTL y guardar debe actualizar la misma regla. Si se elimina una regla de prueba desde su confirmación, debe desaparecer de la lista también al recargar.

Para probar las claves, abrir **Editar regla → Crear clave**, copiar el valor y ocultarlo. Al recargar debe quedar solo su identificador. Eliminar esa clave de prueba desde la confirmación y recargar: ya no debe aparecer.

## Referencias

- [Firebase Authentication con correo y contraseña](https://firebase.google.com/docs/auth/web/password-auth): registro e inicio de sesión con el SDK web.
- [Persistencia de la sesión](https://firebase.google.com/docs/auth/web/auth-state-persistence): manejo de la sesión en el navegador.
