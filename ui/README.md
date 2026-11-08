# UI

Panel con React, Vite y Tailwind CSS. El registro, inicio y cierre de sesión usan Firebase Authentication. Los dominios, las reglas de URL, las API keys y los usuarios de recursos están conectados a la API y Firestore.

Publicado en [zonal-cache-ui.vercel.app](https://zonal-cache-ui.vercel.app), con `VITE_API_URL=https://zonal-cache-api.vercel.app`. El proyecto de Vercel usa la raíz `ui` del mismo repositorio que la API.

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

**Agregar dominio** guarda el nombre, el servidor de origen y el límite de caché. El origen debe ser una dirección HTTP o HTTPS, con puerto opcional y sin rutas, como `http://apache-apache` para el Apache del cluster. La caché debe poder acceder a ese servidor. La UI muestra MB y la API recibe bytes (1 MB = 1 048 576 bytes). Al guardarlo aparece como pendiente y se pueden consultar los datos de su registro TXT.

**Verificar dominio** consulta el TXT mediante la API. Si coincide, el estado cambia a **Verificado** también en la lista de dominios. Si falta o es distinto, muestra el error y permite volver a intentarlo. **Eliminar** pide confirmación y borra el dominio con sus reglas, usuarios y claves.

El detalle del dominio muestra su servidor de origen. Si fue creado antes de agregar este campo, muestra **Sin configurar**; no se cambia su configuración automáticamente.

Desde el detalle se pueden agregar, editar y eliminar reglas de URL. El formulario guarda el patrón, el tamaño de caché, el mecanismo de acceso y los tipos de archivo con su TTL y política. Convierte MB a bytes al guardar. Después vuelve a la lista y consulta las reglas guardadas. Para eliminar una regla se pide confirmación.

En **Editar regla**, si el acceso guardado es por API key, se pueden crear y eliminar claves. Una clave nueva se muestra una sola vez para copiarla; **Ocultar clave** retira su valor de la pantalla. La lista muestra solo los identificadores. La eliminación pide confirmación y llega a las cachés cuando actualizan su configuración. Si se cambia el mecanismo de acceso, hay que guardar la regla antes de administrar sus claves o usuarios.

Si el acceso guardado es **Usuario y contraseña**, se pueden agregar, editar y eliminar los usuarios de esa regla. Al editar se puede cambiar el nombre; dejar la contraseña vacía conserva la actual. La lista muestra solo los nombres y la eliminación pide confirmación. Las contraseñas se envían a la API para guardar su hash y no se vuelven a mostrar.

`#/acceso-recurso?domain=...&path=...&callback=...` es el formulario público al que redirige la caché. Muestra el dominio y la ruta recibidos, envía las credenciales a `POST /api/cache/sessions` y vuelve a la caché con el token firmado. No usa la sesión del panel. Sin un enlace válido indica que se debe abrir el recurso desde su dirección original. **Cerrar sesión del recurso** abre `/__zc/logout` del mismo dominio.

## Comprobar

```bash
npm run build
```

Para probar el flujo completo, crear una cuenta desde la UI, recargar la página, cerrar sesión e ingresar de nuevo. Sin sesión, abrir `#/dominios` debe volver a **Iniciar sesión**. Contraseñas distintas en el registro deben mostrar un mensaje.

Para probar las operaciones del panel, agregar un dominio temporal como `pruebas-panel.local`, con origen `http://apache-apache` y un límite de 1 MB. Abrirlo y recargar: debe conservar el origen y seguir pendiente. Intentar agregarlo otra vez debe mostrar un error. Este nombre no confirma propiedad mediante DNS público y queda fuera de la configuración de las cachés.

Para probar reglas en ese dominio, agregar `/prueba-panel/*` con 0.25 MB, tipo `html`, TTL de 60 segundos, política `LRU` y acceso por API key. Guardar, recargar y abrir **Editar**: los datos deben conservarse. Cambiar a 0.5 MB, TTL de 90 segundos y política `FIFO`; guardar debe actualizar la misma regla. Si se elimina desde su confirmación, debe desaparecer de la lista también al recargar.

Para probar las claves, abrir **Editar regla → Crear clave**, copiar el valor y ocultarlo. Al recargar debe quedar solo su identificador. Eliminar esa clave de prueba desde la confirmación y recargar: ya no debe aparecer.

Para probar usuarios, guardar una regla con acceso por **Usuario y contraseña**. Agregar un usuario de prueba y recargar: debe conservarse su nombre. Editarlo con contraseña vacía debe conservar la anterior; escribir una nueva debe reemplazarla. Probar también un nombre repetido y la eliminación desde su confirmación.

Para probar el TXT, usar un dominio cuyo DNS se pueda modificar. Publicar el nombre y valor que muestra **Verificar dominio** y pulsar **Verificar**. Recargar y comprobar que sigue verificado. Un TXT distinto o ausente debe mantenerlo pendiente. Con `apache.local` no se completa esta prueba en DNS público.

Para probar el login completo, configurar la caché con la API, la URL de esta UI y el mismo secreto de sesión. Abrir un recurso de una regla con contraseña en un dominio verificado: debe redirigir al formulario, aceptar su usuario y regresar al archivo. Una contraseña incorrecta debe mostrar un error. Abrir el enlace de cierre de sesión y volver a pedir el archivo debe solicitar el login otra vez.

Para probar la eliminación, usar un dominio de prueba que ya no se necesite, confirmar **Eliminar** y recargar. No debe aparecer en la lista ni conservar sus subcolecciones en Firestore.

El 08/11/2026 se probaron en Vercel la creación, edición y eliminación de reglas y usuarios, la creación y eliminación de claves y la eliminación del dominio temporal. Los datos guardados se conservaron al recargar; los eliminados no reaparecieron. El formulario confirmó el cambio de contraseña, pero no se probó el login con esa contraseña. Los datos temporales se borraron al terminar.

También se verificó `proxycache.duckdns.org` mediante un TXT de DuckDNS. El dominio, su regla `/*` y el usuario `prueba3` se conservaron para la prueba de integración. Falta probar el login válido, Apache con MISS/HIT y el cierre de sesión con la caché desplegada. Ver [resultados y pasos](../README.md#92-pruebas-de-api-y-ui-en-vercel).

## Referencias

- [Firebase Authentication con correo y contraseña](https://firebase.google.com/docs/auth/web/password-auth): registro e inicio de sesión con el SDK web.
- [Persistencia de la sesión](https://firebase.google.com/docs/auth/web/auth-state-persistence): manejo de la sesión en el navegador.
