# 2026-02-2023064329-IC7602-P2

# Proyecto 2: Proxy/Caché de aplicación con geolocalización

**Curso:** Redes (IC 7602) · Escuela de Ingeniería en Computación · Tecnológico de Costa Rica · II Semestre 2026

## Tabla de contenidos

1. [Descripción general](#1-descripción-general)
2. [Integrantes](#2-integrantes)
3. [Arquitectura](#3-arquitectura)
4. [Estructura del repositorio](#4-estructura-del-repositorio)
5. [Requisitos previos](#5-requisitos-previos)
6. [Cómo ejecutar el proyecto](#6-cómo-ejecutar-el-proyecto)
7. [Configuración (variables de entorno)](#7-configuración-variables-de-entorno)
8. [Documentación por componente](#8-documentación-por-componente)
9. [Pruebas realizadas](#9-pruebas-realizadas)
10. [Estado de la implementación](#10-estado-de-la-implementación)
11. [Conclusiones](#11-conclusiones)
12. [Recomendaciones](#12-recomendaciones)
13. [Reportes de avance](#13-reportes-de-avance)
14. [Uso de inteligencia artificial generativa](#14-uso-de-inteligencia-artificial-generativa)
15. [Referencias y créditos](#15-referencias-y-créditos)

---

## 1. Descripción general

> Esta documentación está escrita para una persona con conocimientos básicos de computación: debe poder ejecutar y probar el proyecto siguiéndola paso a paso.

**Objetivo general:** implementar un proxy/caché de aplicación con geolocalización.

El sistema intercepta peticiones DNS de dominios específicos, las redirige a una caché HTTP zonal según el país de origen de la persona usuaria, y sirve el contenido desde disco o desde el origen. Cada dominio, URL, política de reemplazo y mecanismo de autenticación se administra desde una interfaz web.

**Objetivos específicos:**

- Desarrollar servidores UDP y TCP en GNU C.
- Procesar peticiones DNS especificadas en el RFC 2929.
- Desarrollar servicios de caché con algoritmos de reemplazo.
- Diseñar servicios de autenticación para APIs y sitios web.
- Diseñar e implementar una aplicación web con inteligencia artificial generativa.
- Implementar una API REST en Java.
- Automatizar la solución con Docker, Helm Charts, Terraform, Docker Compose, Kubernetes o proveedores de nube.

**Lenguajes y tecnologías elegidos** (el enunciado exige documentarlos; **no se permite Python**):

| Componente         | Lenguaje / tecnología                  | Dónde se ejecuta  |
| ------------------ | -------------------------------------- | ----------------- |
| DNS Interceptor    | GNU C                                  | Kubernetes (Helm) |
| Zonal Cache        | GNU C (OpenSSL, libcurl, cJSON)        | Kubernetes (Helm) |
| Rest API           | Java 21 (servidor HTTP del JDK)        | Kubernetes (Helm) |
| Apache Server      | Apache HTTP Server 2.4                 | Kubernetes (Helm) |
| REST API (Node.js) | Node.js 24 + Express + Firebase/Firestore | Vercel         |
| UI                 | React + Vite + Tailwind CSS            | Vercel            |

## 2. Integrantes

| Carné      | Nombre                       | Componentes a cargo                                                                            |
| ---------- | ---------------------------- | ---------------------------------------------------------------------------------------------- |
| 2023064329 | Luis Fernando Ureña Corrales | DNS Interceptor, Apache, Rest API Java, deploy k8s + Helm, librería de algoritmos de reemplazo |
| 2023       | Fabricio Herrera Rodriguez   | Zonal Cache                                                                                    |
| 2023       | Nicole Parra Valverde        | REST API Node.js, UI y su deploy en Vercel                                                     |

## 3. Arquitectura

```text
Cliente ──DNS (UDP)──▶ DNS Interceptor ──(IP to Country)──▶ IP de la Zonal Cache de su zona
   │
   └──HTTP/HTTPS (TCP)──▶ Zonal Cache (america | europa | asia)
                              │
            ┌─────────────────┼──────────────────┐
            ▼                 ▼                  ▼
     Rest API (Java)    Apache Server     Sitios externos (HTTP / HTTPS)

Zonal Cache ──GET /api/cache/config (cada 30 s)──▶ REST API Node.js ──▶ Firebase/Firestore ◀── UI (React)
```

**Flujo de una petición:**

1. La persona usuaria consulta un dominio registrado. El **DNS Interceptor** determina su país con la base IP to Country y responde con la IP de la **Zonal Cache** de su zona. Los demás dominios se reenvían a un DNS externo.
2. La Zonal Cache obtiene su configuración desde Firebase a través del API Node.js (o, en pruebas locales, desde un archivo JSON) y la recarga periódicamente.
3. Identifica el dominio por el encabezado `Host` y la regla por la ruta, y valida `x-api-key` o la sesión de usuario/contraseña si la regla lo exige.
4. Si el recurso está en caché y no expiró, lo sirve (`X-Cache: HIT`). Si no, lo pide al origen, lo guarda aplicando el TTL, el límite de tamaño y la política de reemplazo, y lo sirve (`X-Cache: MISS`).

Diagramas detallados (secuencia de una petición, decisión dentro de la caché, configuración desde Firestore, login y despliegue) en [Docs/Integracion.md](Docs/Integracion.md#4-flujos) y [zonal-cache/README.md](zonal-cache/README.md#1-flujo-de-una-petición).

## 4. Estructura del repositorio

```text
.
├── Docs/
│   ├── Integracion.md         # Integración entre componentes, flujos y pendientes
│   ├── Contratos/API-Node.md  # Datos y endpoints del API Node.js
│   └── Reportes/              # Reportes semanales de avance
├── dns-interceptor/           # GNU C: servidor UDP, IP to Country, pruebas, Dockerfile, Compose
├── zonal-cache/               # GNU C: servidor TCP/TLS, caché en disco, pruebas, Dockerfile, Compose
├── rest-api-java/             # Java: API con GET, POST, PUT y DELETE
├── apache/                    # Apache + HTML básico
├── api-node/                  # Node.js + Express (Vercel)
├── ui/                        # React + Vite + Tailwind (Vercel)
├── deploy/
│   ├── helm/                  # Charts: apache, rest-api-java, zonal-cache, dns-interceptor
│   ├── docker-compose.yml     # Apache + Rest API Java sin Kubernetes
│   └── test-k8s.sh            # Pruebas de integración automáticas sobre Kubernetes
├── .github/workflows/         # CI de la Zonal Cache (pruebas unitarias y helm lint)
└── README.md
```

## 5. Requisitos previos

Versiones con las que se probó el proyecto:

| Herramienta                  | Versión probada                     | Para qué se usa                                   |
| ---------------------------- | ----------------------------------- | ------------------------------------------------- |
| Docker (Docker Desktop)      | 29.2                                | Construir imágenes y ejecutar Docker Compose      |
| Kubernetes                   | 1.34 (Kubernetes de Docker Desktop) | Cluster local                                     |
| kubectl                      | 1.35                                | Administrar el cluster                            |
| Helm                         | 4.3                                 | Instalar los charts                               |
| GCC / Make (opcional)        | 13.3 / 4.3                          | Compilar y probar los componentes en C sin Docker |
| JDK                          | 21 (dentro de la imagen, sin Maven) | Compilar la Rest API Java                         |
| Node.js                      | 24.x                                | API Node.js y UI                                  |
| `dig`, `nslookup`, `curl`    | —                                   | Probar el sistema                                 |

No hace falta instalar GCC ni el JDK: los Dockerfiles compilan cada componente.

## 6. Cómo ejecutar el proyecto

> **Toda la instalación está automatizada** con Dockerfiles, Docker Compose y Helm Charts.

### 6.1 Clonar el repositorio

```bash
git clone https://github.com/LuisFernandoUC/2026-02-2023064329-IC7602-P2.git
cd 2026-02-2023064329-IC7602-P2
```

### 6.2 Preparar Kubernetes y Helm

1. En Docker Desktop: **Settings → Kubernetes → Enable Kubernetes** (modo *kubeadm* si se ofrece) → **Apply & Restart**. Con este modo el cluster usa directamente las imágenes construidas con `docker build`.
2. Instalar `kubectl` y `helm`. Por ejemplo, en Ubuntu:

   ```bash
   sudo snap install kubectl --classic
   sudo snap install helm --classic
   kubectl config use-context docker-desktop
   kubectl get nodes          # el nodo docker-desktop debe estar Ready
   ```

No hay valores quemados en el código: toda la configuración se inyecta con variables de entorno ([sección 7](#7-configuración-variables-de-entorno)), que los charts toman de sus `values.yaml`.

### 6.3 Construir las imágenes

```bash
docker build -t apache-p2       apache
docker build -t rest-api-java   rest-api-java
docker build -t zonal-cache     zonal-cache
docker build -t dns-interceptor dns-interceptor
```

### 6.4 Desplegar con Helm

```bash
helm install apache          deploy/helm/apache
helm install rest-api-java   deploy/helm/rest-api-java
helm install zonal-cache     deploy/helm/zonal-cache -f deploy/helm/zonal-cache/values-local.yaml
kubectl wait --for=condition=available deploy --all --timeout=120s
helm install dns-interceptor deploy/helm/dns-interceptor      # siempre de último

kubectl get pods,svc
```

- El **orden importa**: al instalarse, el DNS Interceptor busca la IP de los Services `zonal-cache-zonal-cache-<zona>`. Si se instaló antes que la caché: `helm upgrade dns-interceptor deploy/helm/dns-interceptor`.
- [values-local.yaml](deploy/helm/zonal-cache/values-local.yaml) le da a la caché una configuración fija con Apache (`apache.p2.local`) y la Rest API Java (`api.cdn.p2.local`). Sirve para probar sin depender del API Node.js (ver [sección 10](#10-estado-de-la-implementación)).
- Para usar la configuración de Firebase en su lugar:

  ```bash
  helm upgrade --install zonal-cache deploy/helm/zonal-cache \
    --set config.url=https://<api>.vercel.app/api/cache/config \
    --set secrets.configToken=$CACHE_API_TOKEN \
    --set secrets.sessionSecret=$CACHE_SESSION_SECRET \
    --set loginUrl='https://<ui>.vercel.app/#/acceso-recurso'
  ```

- Si un pod queda en `ErrImagePull`, agregar `--set image.pullPolicy=Never` al `helm install` de ese chart.

### 6.5 Desplegar el API Node.js y la UI (Vercel)

1. En Vercel, importar el repositorio de GitHub dos veces: un proyecto con **Root Directory** `api-node` y otro con `ui`. Cada push a la rama configurada despliega automáticamente.
2. Configurar las variables de entorno de cada proyecto ([sección 7](#7-configuración-variables-de-entorno)).
3. Ejecución local, como alternativa (requiere Node.js 24):

   ```bash
   cd api-node && cp .env.example .env    # completar FIREBASE_* y CACHE_API_TOKEN
   npm ci && npm run check:firestore && npm run dev

   cd ui && cp .env.example .env          # completar VITE_FIREBASE_* y VITE_API_URL
   npm ci && npm run dev                  # http://localhost:5173
   ```

Detalles en [api-node/README.md](api-node/README.md) y [ui/README.md](ui/README.md).

### 6.6 Verificar que todo funciona

El script [deploy/test-k8s.sh](deploy/test-k8s.sh) ejecuta todas las pruebas de integración contra el cluster. Crea un pod temporal con `curl`, `dig` y `openssl`, porque las IPs de las cachés son `ClusterIP` y solo se alcanzan desde dentro del cluster.

```bash
deploy/test-k8s.sh            # pruebas de integración (~30 s)
deploy/test-k8s.sh --unit     # además, las pruebas unitarias de la caché y del DNS Interceptor
deploy/test-k8s.sh --keep     # conserva el pod de pruebas para seguir probando a mano
```

Termina con código `0` si todas las pruebas pasan. Salida resumida:

```text
== 2. DNS Interceptor (geolocalización por EDNS Client Subnet)
  ✔ apache.p2.local desde 80.58.61.0/24 -> 10.102.13.205 (caché europa)
  ...
== 3. Zonal Cache por zona (health, MISS -> HIT, independencia)
  ✔ [europa] Apache 1.ª petición (200, X-Cache: MISS, zona: europa)
  ✔ [europa] Apache 2.ª petición (200, X-Cache: HIT, zona: europa)
  ...
== Resumen
  47 correctas, 0 fallidas
```

Pruebas manuales equivalentes:

```bash
# DNS desde el equipo (NodePort 30053); +subnet simula el país del cliente
dig +short @127.0.0.1 -p 30053 apache.p2.local +subnet=80.58.61.0/24   # IP de la caché europa
nslookup -port=30053 apache.p2.local 127.0.0.1                         # zona por defecto (america)

# HTTP desde un pod del cluster
kubectl run cliente --rm -it --image=curlimages/curl --restart=Never -- sh
IP=<CLUSTER-IP de zonal-cache-zonal-cache-europa>
curl -si --resolve apache.p2.local:80:$IP http://apache.p2.local/ | grep -i x-cache      # MISS, luego HIT
curl -sik --resolve apache.p2.local:443:$IP https://apache.p2.local/ | grep -i x-cache   # SSL termination
curl -si -H 'x-api-key: clave-valida' --resolve api.cdn.p2.local:80:$IP http://api.cdn.p2.local/api/items
```

`clave-valida` es la API key de prueba: `values-local.yaml` guarda su SHA-256 (`echo -n clave-valida | sha256sum`).

### 6.7 Entornos con Docker Compose (sin Kubernetes)

| Entorno                          | Comando                                                         | Qué permite probar                                                               |
| -------------------------------- | --------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| Caché + Apache + sitios externos | `docker compose -f zonal-cache/docker-compose.yml up --build`   | HIT/MISS, HTTPS, API key, redirección al login, sitios externos HTTP y HTTPS     |
| DNS Interceptor                  | `cd dns-interceptor && docker compose up --build`               | Geolocalización con `dig @127.0.0.1 -p 15353 ... +subnet=...`                    |
| Apache + Rest API Java           | `docker compose -f deploy/docker-compose.yml up --build`        | Los orígenes directos (puertos 8081 y 8080)                                      |

Los comandos de prueba de cada entorno están en [zonal-cache/README.md](zonal-cache/README.md#entorno-local-completo-con-docker-compose), [dns-interceptor/README.md](dns-interceptor/README.md#con-docker-compose-desarrollo) y [rest-api-java/README.md](rest-api-java/README.md#pruebas).

### 6.8 Desinstalar / limpiar

```bash
helm uninstall dns-interceptor zonal-cache rest-api-java apache
docker compose -f zonal-cache/docker-compose.yml down     # si se usó Docker Compose
```

Para borrar el cluster: Docker Desktop → Settings → Kubernetes → **Reset Kubernetes Cluster**.

## 7. Configuración (variables de entorno)

Toda configuración se inyecta por variables de entorno. En Kubernetes las definen los charts a partir de sus `values.yaml`.

| Componente      | Variable                                  | Descripción                                                    | Ejemplo / valor por defecto              |
| --------------- | ----------------------------------------- | -------------------------------------------------------------- | ---------------------------------------- |
| DNS Interceptor | `DI_ZONE_IPS`                             | IP de cada caché zonal                                         | `america=10.0.0.1,europa=...` (obligatoria; el chart la calcula) |
| DNS Interceptor | `DI_DOMAINS`                              | Dominios interceptados, admite `*.dominio`                     | `apache.p2.local,*.cdn.p2.local`         |
| DNS Interceptor | `DI_DEFAULT_ZONE`                         | Zona para países desconocidos o IP privadas                    | `america`                                |
| DNS Interceptor | `DI_UPSTREAM_DNS`, `DI_UPSTREAM_PORT`     | DNS externo para los demás dominios                            | `8.8.8.8`, `53`                          |
| DNS Interceptor | `DI_PORT`, `DI_TTL`, ...                  | Ver la tabla completa en [dns-interceptor/README.md](dns-interceptor/README.md#variables-de-entorno) | `5353`, `30` |
| Zonal Cache     | `ZC_*`                                    | Ver la tabla completa en [zonal-cache/README.md](zonal-cache/README.md#3-variables-de-entorno) | —           |
| Rest API Java   | `PORT`                                    | Puerto en el que escucha                                       | `8080`                                   |
| API Node.js     | `HOST`, `PORT`                            | Dirección y puerto del servidor local                          | `127.0.0.1`, `3000`                      |
| API Node.js     | `UI_ORIGIN`                               | Origen permitido por CORS                                      | `http://127.0.0.1:5173`                  |
| API Node.js     | `FIREBASE_PROJECT_ID`                     | ID del proyecto de Firebase                                    | `project_id` de la cuenta de servicio    |
| API Node.js     | `FIREBASE_CLIENT_EMAIL`                   | Correo de la cuenta de servicio                                | `client_email`                           |
| API Node.js     | `FIREBASE_PRIVATE_KEY`                    | Clave privada de la cuenta de servicio                         | `private_key`, entre comillas dobles     |
| API Node.js     | `CACHE_API_TOKEN`                         | Token con el que se autentican las cachés; igual a `ZC_CONFIG_TOKEN` | `openssl rand -hex 32`             |
| UI              | `VITE_API_URL`                            | URL del API Node.js                                            | `http://127.0.0.1:3000`                  |
| UI              | `VITE_FIREBASE_API_KEY`, `VITE_FIREBASE_AUTH_DOMAIN`, `VITE_FIREBASE_PROJECT_ID`, `VITE_FIREBASE_APP_ID` | Configuración web de Firebase Authentication | Consola de Firebase |

## 8. Documentación por componente

### 8.1 DNS Interceptor

Servidor **UDP en GNU C**, adaptado del Proyecto 1. Para cada consulta:

1. Si el dominio está en `DI_DOMAINS` (admite `*.dominio`), obtiene la IP del cliente, o la subred de EDNS Client Subnet si viene, y busca su país en la base _IP to Country_ de db-ip.com (búsqueda binaria sobre ~357 mil rangos en memoria).
2. Convierte el país en zona con [data/country_zones.csv](dns-interceptor/data/country_zones.csv) y responde un registro A con la IP de la caché de esa zona.
3. Si el dominio no está interceptado, reenvía la consulta al DNS externo (`DI_UPSTREAM_DNS`) y devuelve su respuesta.

Se despliega con [deploy/helm/dns-interceptor](deploy/helm/dns-interceptor) (Service `NodePort` 30053/UDP). Documentación completa: [dns-interceptor/README.md](dns-interceptor/README.md).

### 8.2 Zonal Cache

Proxy inverso HTTP/HTTPS en **GNU C**, con un Deployment y un Service por zona. Documentación completa en [zonal-cache/README.md](zonal-cache/README.md):

- flujo de una petición y encabezados que se envían al origen;
- caché en disco con límites por dominio y por URL;
- algoritmos de reemplazo (LRU, LFU, FIFO, MRU, Random);
- autenticación con `x-api-key` y con usuario/contraseña (sesión JWT);
- SSL termination y contrato con el API Node.js;
- pruebas y despliegue con Helm ([deploy/helm/zonal-cache](deploy/helm/zonal-cache)).

### 8.3 Rest API (Java)

API en **Java 21** que usa solo el servidor HTTP del JDK (sin Maven ni frameworks). Implementa GET, POST, PUT y DELETE sobre `/api/items`, más `/health` y `/api/hello`. Los datos viven en memoria. Se compila dentro del Dockerfile y se despliega con [deploy/helm/rest-api-java](deploy/helm/rest-api-java). Endpoints y pruebas: [rest-api-java/README.md](rest-api-java/README.md).

### 8.4 Apache Server

Imagen `httpd:2.4-alpine` que sirve [apache/html/index.html](apache/html/index.html). Es el origen del dominio `apache.p2.local`. Se despliega con [deploy/helm/apache](deploy/helm/apache). Ver [apache/README.md](apache/README.md).

### 8.5 REST API Node.js

La API incluye `GET /api/health` y estos grupos de rutas:

- **Dominios** (`/api/domains`): listado y registro. Verifican la sesión con Firebase Authentication y usan el UID de la cuenta como propietario. Al registrar un dominio se pide el servidor de origen (`originUrl`), que debe ser una URL HTTP o HTTPS sin ruta.
- **Reglas de URL** (`/api/domains/:domainId/rules`): crear, consultar, editar y eliminar, con permisos por propietario. Cada regla tiene ruta, tamaño máximo (no mayor que el del dominio), modo de autenticación (`api-key` o `password`) y, por tipo de archivo, TTL y política de reemplazo.
- **Configuración para las cachés** (`GET /api/cache/config`): entrega a las cachés autenticadas los dominios verificados, sus reglas y los hashes de las API keys. Requiere `CACHE_API_TOKEN`, que debe coincidir con `ZC_CONFIG_TOKEN` en la caché.

La conexión a Firestore se configura con variables de entorno. El comando `npm run check:firestore`, desde `api-node/`, permite comprobar el acceso.

Siguen pendientes la verificación TXT, la eliminación de dominios, la administración de API keys y usuarios, y el login de recursos (`POST /api/cache/sessions`). Ver [api-node/README.md](api-node/README.md) y [Docs/Contratos/API-Node.md](Docs/Contratos/API-Node.md).

### 8.6 UI

La UI usa React, Vite y Tailwind CSS. El registro, inicio y cierre de sesión están conectados a Firebase Authentication. Los dominios (con su servidor de origen) y las reglas de URL usan la API y Firestore; las reglas se pueden agregar, editar y eliminar. La verificación TXT, la eliminación de dominios, las credenciales y el formulario de acceso a recursos siguen pendientes. Ver [ui/README.md](ui/README.md).

### 8.7 Automatización

| Herramienta        | Archivos                                                                                       | Uso                                                                  |
| ------------------ | ---------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| Dockerfile         | `dns-interceptor/`, `zonal-cache/`, `rest-api-java/`, `apache/`                                | Compilación en varias etapas; imágenes finales sin compiladores y con usuario sin privilegios |
| Docker Compose     | `zonal-cache/docker-compose.yml`, `dns-interceptor/docker-compose.yml`, `deploy/docker-compose.yml` | Entornos de desarrollo sin Kubernetes                           |
| Helm Charts        | `deploy/helm/{apache,rest-api-java,zonal-cache,dns-interceptor}`                               | Despliegue en Kubernetes; la caché crea un Deployment y un Service por zona |
| Script de pruebas  | `deploy/test-k8s.sh`                                                                           | Pruebas de integración automáticas sobre el cluster                  |
| GitHub Actions     | `.github/workflows/zonal-cache.yml`                                                            | Pruebas unitarias, imagen de pruebas, `helm lint` y `helm template`  |

No se usó Terraform porque todo se ejecuta en Kubernetes local.

## 9. Pruebas realizadas

Resultados obtenidos el 03/11/2026 con Kubernetes de Docker Desktop y los cuatro charts instalados como en la [sección 6.4](#64-desplegar-con-helm).

| #   | Prueba                                              | Pasos para reproducir | Resultado esperado | Resultado obtenido |
| --- | --------------------------------------------------- | --------------------- | ------------------ | ------------------ |
| 1   | Rest API (Java) a través de la caché                | `deploy/test-k8s.sh`, sección 5 | GET `200` (MISS y luego HIT), POST `201`, PUT `200`, DELETE `204`, GET del item borrado `404`. Los métodos que modifican dan `X-Cache: BYPASS` | Correcto |
| 2   | Apache con HTML simple                              | `deploy/test-k8s.sh`, sección 3 | `200`, `X-Cache: MISS` y luego `HIT` con encabezado `Age`, en cada zona | Correcto en las tres zonas |
| 3   | Sitio externo con **HTTP**                          | `docker compose -f zonal-cache/docker-compose.yml up --build`; `curl -i -H "Host: example.com" http://localhost:8080/` dos veces | `200`, MISS y luego HIT | Correcto |
| 4   | Sitio externo con **HTTPS**                         | Mismo entorno; `curl -i -H "Host: www.wikipedia.org" http://localhost:8080/` dos veces | `200`, MISS y luego HIT (la caché habla HTTPS con el origen) | Correcto |
| 5   | Resolución DNS con geolocalización                  | `deploy/test-k8s.sh`, sección 2, o `dig @127.0.0.1 -p 30053 apache.p2.local +subnet=<subred>` | EE. UU. → america, España → europa, Japón → asia; comodín `*.cdn.p2.local`; `nslookup` sin subred → zona por defecto; google.com se reenvía | Correcto, también desde el equipo por el NodePort 30053 |
| 6   | Autenticación con `x-api-key`                       | `deploy/test-k8s.sh`, sección 5 | Sin clave `401`, clave incorrecta `401`, `clave-valida` `200` | Correcto |
| 7   | Autenticación con usuario y contraseña (sesión)     | Entorno Compose de la caché; `curl -i -H "Host: apache.local" http://localhost:8080/privado/secreto.html` | `302` al login de la UI con `domain`, `path` y `callback` | Correcto. El inicio de sesión completo depende de `POST /api/cache/sessions`, pendiente en el API. La verificación del JWT y la cookie se prueban en las pruebas unitarias (`proxy`, `auth`) |
| 8   | Algoritmos de reemplazo (LRU, LFU, FIFO, MRU, Random) | `docker build --target test zonal-cache` o `deploy/test-k8s.sh --unit` | Cada política expulsa la víctima esperada | `98 pruebas, 0 fallidas` |
| 9   | Límite de tamaño por dominio y por URL              | Igual que la #8 (suite `cache`) | No se superan los límites; un objeto más grande que el límite no se guarda | Correcto |
| 10  | SSL termination                                     | `deploy/test-k8s.sh`, sección 4 | HTTPS `200` con MISS y luego HIT; certificado `CN=zonal-cache` | Correcto |
| 11  | Respuestas propias de la caché                      | `deploy/test-k8s.sh`, sección 6 | Dominio no administrado `404`; petición repetida entre cachés `508` | Correcto |
| 12  | Pruebas unitarias del DNS Interceptor               | `cd dns-interceptor && make test` | Sin fallas | `106 verificaciones, 0 fallidas` |
| 13  | Conexión API Node.js con Firestore                  | Desde `api-node/`, configurar `.env` y ejecutar `npm run check:firestore` | `Conexión con Firestore verificada.` | Correcto; consulta de lectura realizada |
| 14  | Charts de Helm                                      | `helm lint deploy/helm/<chart>` | Sin errores | Los cuatro charts pasan |

**Comportamiento esperado, documentado como limitación:** después de un POST a `/api/items`, la lista cacheada sigue sin el item nuevo hasta que vence su TTL (10 s en `values-local.yaml`). El script lo muestra como aviso.

Recomendación del enunciado: usar `nslookup` para el DNS Interceptor y Postman/`curl` para la caché zonal.

## 10. Estado de la implementación

| Aspecto                  | Funciona | No funciona / limitaciones | Notas |
| ------------------------ | -------- | -------------------------- | ----- |
| DNS Interceptor          | Respuestas A según el país (IP to Country y EDNS Client Subnet), comodines, zona por defecto, reenvío al DNS externo, despliegue con Helm que obtiene solo las IPs de las cachés, pruebas unitarias | Solo IPv4 y UDP. La lista de dominios es fija en `values.yaml` y no se sincroniza con los dominios verificados en la UI. Con `NodePort` se pierde la IP real del cliente | Ver [dns-interceptor/README.md](dns-interceptor/README.md#qué-funciona-y-qué-no) |
| Zonal Cache              | Servidor TCP con SSL termination, caché en disco con límites por dominio y por URL, LRU/LFU/FIFO/MRU/Random, TTL por tipo de archivo, `x-api-key`, verificación de sesiones JWT, redirección al login, orígenes HTTP y HTTPS, recarga de configuración, un despliegue por zona | La configuración desde Firebase está implementada en la caché, pero en la demostración se usa `values-local.yaml`, porque el API todavía no marca dominios como verificados. Sin keep-alive; no respeta `Vary` | Ver [zonal-cache/README.md](zonal-cache/README.md#9-estado) |
| RestAPI Node.js / UI     | Registro, inicio y cierre de sesión con Firebase Authentication; registro y listado de dominios con servidor de origen; administración de reglas de URL; `GET /api/cache/config` para las cachés | Falta: verificación TXT, eliminación de dominios, crear y eliminar API keys, crear, modificar y eliminar usuarios, login de recursos (`POST /api/cache/sessions`), reglas sin autenticación y tipo de archivo `*` | Sin la verificación TXT, `/api/cache/config` devuelve una lista vacía. Ver [Docs/Integracion.md](Docs/Integracion.md#9-pendientes) |
| Rest API / Apache Server | GET, POST, PUT y DELETE en Java; HTML en Apache; ambos con Dockerfile, Helm y Docker Compose, probados a través de la caché | Los datos de la Rest API Java viven en memoria y se pierden al reiniciar el pod | — |

## 11. Conclusiones

1. Separar la lógica del proxy de los sockets (`proxy_handle` recibe una función de origen) permitió probar el flujo completo de la caché sin red, con un origen falso.
2. EDNS Client Subnet hace posible probar la geolocalización desde una sola máquina: sin esa extensión, todas las consultas llegarían con la IP privada del cluster y caerían en la zona por defecto.
3. La caché solo ofrece garantías reales si TTL y política de reemplazo trabajan juntos: el TTL decide cuándo un objeto deja de ser válido y la política decide quién sale cuando falta espacio.
4. Dar a cada regla de URL su propia partición, con límite y política propios, evita que una ruta muy activa expulse el contenido de las demás rutas del dominio.
5. Guardar solo el SHA-256 de las API keys y compararlo en tiempo constante protege las claves aunque se filtre la configuración.
6. Validar las sesiones con un JWT firmado permite autenticar cada petición sin consultar al API, a cambio de no poder revocar una sesión antes de que venza.
7. La SSL termination simplifica los orígenes, que no necesitan certificados, y concentra el manejo de TLS en un solo componente.
8. Kubernetes y Helm permiten desplegar varias zonas a partir de un solo chart. Generar un Deployment y un Service por zona fue clave para que el DNS Interceptor encontrara las IPs automáticamente.
9. Inyectar toda la configuración por variables de entorno permitió usar la misma imagen en Docker Compose, en Kubernetes y en el CI.
10. Acordar un contrato de datos temprano y aceptar en la caché los dos formatos de nombres de campos redujo el costo de integrar componentes desarrollados en paralelo.
11. Las pruebas de integración automáticas (`deploy/test-k8s.sh`) detectan en segundos problemas que a mano tomarían varios minutos de comandos repetidos.

## 12. Recomendaciones

1. Completar la verificación TXT en el API para que la configuración real de Firebase llegue a las cachés y dejar `values-local.yaml` solo para desarrollo.
2. Permitir reglas sin autenticación y el tipo de archivo `*` en el API y la UI: la caché ya los soporta y son necesarios para cachear contenido público.
3. Sincronizar la lista de dominios del DNS Interceptor con los dominios verificados del API, en lugar de mantenerla fija en `values.yaml`.
4. Agregar keep-alive y un pool de hilos con `epoll` a la caché si se espera mucha concurrencia, para no crear un hilo y un handshake TLS por petición.
5. Invalidar en la caché las entradas de una ruta cuando pasa por ella un POST, PUT o DELETE, para no servir datos viejos hasta que venza el TTL.
6. Persistir el índice de la caché en disco, para no perder el contenido al reiniciar un pod.
7. Publicar las cachés con `LoadBalancer` o `externalTrafficPolicy: Local` en un cluster real, para que los clientes externos lleguen a ellas y se conserve su IP real.
8. Usar certificados de una autoridad reconocida (por ejemplo, cert-manager con Let's Encrypt) en lugar del certificado autofirmado del chart.
9. Agregar a GitHub Actions el DNS Interceptor, el API Node.js y el script de integración sobre un cluster efímero (kind), no solo la caché.
10. Respetar el encabezado `Vary` del origen, o incluirlo en la clave de caché, antes de cachear respuestas que cambian por idioma o por usuario.
11. Integrar los componentes cada semana en lugar de al final: varios huecos (origen del dominio, formato de reglas, modo sin autenticación) solo aparecieron al conectar el API con la caché.

## 13. Reportes de avance

Los reportes semanales se encuentran en [`Docs/Reportes/`](Docs/Reportes/).

| Reporte | Fecha      | Archivo                      |
| ------- | ---------- | ---------------------------- |
| P2R1    | 15/10/2026 | [R1.md](Docs/Reportes/R1.md) |
| P2R2    | 22/10/2026 | [R2.md](Docs/Reportes/R2.md) |
| P2R3    | 29/10/2026 | [R3.md](Docs/Reportes/R3.md) |

## 14. Uso de inteligencia artificial generativa

El uso de IA está regulado por el documento _Política para el Uso de Inteligencia Artificial Generativa_.

| Herramienta | Para qué se usó | Cómo se verificó |
| ----------- | --------------- | ---------------- |
| Claude Code (Anthropic) | Análisis del avance contra el enunciado; corrección del comando del contenedor `certs` en `zonal-cache/docker-compose.yml` y del valor inicial del hash FNV-1a 64 en `zonal-cache/src/util.c`; script `deploy/test-k8s.sh`; actualización de esta documentación | Pruebas unitarias (98 de 98 en la caché, 106 de 106 en el DNS), ejecución del script contra el cluster (47 de 47), `helm lint` de los cuatro charts y revisión del equipo |


## 15. Referencias y créditos

El enunciado exige reconocer al autor de cualquier código o configuración tomado de Internet; usar código sin reconocimiento se considera copia.

| Recurso | Autor / fuente | Dónde se usa |
| ------- | -------------- | ------------ |
| IP to Country Lite (CC BY 4.0) | [db-ip.com](https://db-ip.com) | `dns-interceptor/data/dbip-country-lite.csv.gz` |
| cJSON (MIT) | Dave Gamble y colaboradores, [github.com/DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) | Zonal Cache: JSON de configuración y JWT |
| libcurl | Daniel Stenberg y colaboradores, [curl.se](https://curl.se/libcurl/) | Zonal Cache: peticiones al origen y al API |
| OpenSSL (Apache 2.0) | OpenSSL Project, [openssl.org](https://www.openssl.org/) | Zonal Cache: TLS, SHA-256 y HMAC |
| Apache HTTP Server (imagen `httpd`) | Apache Software Foundation | Servidor Apache de origen |
| Eclipse Temurin (imagen) | Adoptium | Compilación y ejecución de la Rest API Java |
| RFC 1035, RFC 2929, RFC 7871 (EDNS Client Subnet), RFC 7519 (JWT) | IETF | DNS Interceptor y autenticación de la caché |
| Express, firebase-admin, React, Vite, Tailwind CSS | Sus respectivos autores (dependencias en `package.json`) | API Node.js y UI |

Las librerías se instalan como dependencias o paquetes del sistema; no se copió su código.

---

**Fechas importantes:** entrega el **03/11/2026** antes de medianoche (tar.gz + enlace al repositorio) · evaluación presencial el **10/11/2026**.
