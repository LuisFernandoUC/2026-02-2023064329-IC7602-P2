# DNS Interceptor

Servidor DNS en GNU C que intercepta las consultas de ciertos dominios y responde con la IP de la
**caché zonal** que le corresponde a la persona usuaria según su país. Los demás dominios se
reenvían a un DNS externo. Está adaptado del DNS Interceptor del Proyecto 1 (que estaba en Rust y
dependía de una API de DNS).

## Cómo funciona

```text
cliente ── consulta DNS (UDP) ──▶ DNS Interceptor
                                   │
                  ¿el dominio está en DI_DOMAINS?
                   │                              │
                  sí                              no
                   │                              │
   IP del cliente (o subred EDNS)          reenvía la consulta
                   │                       al DNS externo (8.8.8.8)
   país   ◀── base IP to Country (db-ip)           │
                   │                               │
   zona   ◀── tabla país → zona                    │
                   │                               │
   IP de la caché zonal de esa zona                │
                   ▼                               ▼
              respuesta A  ◀───────────── respuesta del DNS externo
```

1. Recibe la consulta por UDP o TCP y valida que sea una consulta estándar (si no lo es, responde FORMERR o NOTIMP).
2. Si el dominio no está en `DI_DOMAINS` ni entre los dominios verificados del API, la reenvía tal cual al DNS externo y devuelve su respuesta.
3. Si está, determina el país de origen y lo traduce a una zona (`america`, `europa` o `asia`).
4. Responde con la IP de la caché zonal de esa zona (registro A, TTL de `DI_TTL` segundos). Si esa caché no responde, usa otra zona sana (ver _Respaldo de zonas_).

**País desconocido:** si la IP no está en la base (por ejemplo una IP privada) o su país no tiene
zona, se usa la zona por defecto (`DI_DEFAULT_ZONE`).

**Consultas que no son tipo A** (por ejemplo AAAA) sobre un dominio interceptado se responden
con éxito y sin registros, para que el cliente use IPv4.

### ¿De dónde sale la IP del cliente?

Se toma, en este orden:

1. La subred indicada en la opción **EDNS Client Subnet** (RFC 7871) si la consulta la trae.
   Es lo que usan los resolvers públicos y permite probar zonas distintas con `dig +subnet`.
2. La IP de origen del paquete UDP.

> En Kubernetes, un Service `NodePort` o `LoadBalancer` cambia por defecto la IP de origen del paquete. El
> chart usa `externalTrafficPolicy: Local` (`service.preserveClientIP`) para conservar la IP real; en
> un cluster de un solo nodo con Docker Desktop el cliente igual llega con la IP del gateway de la VM,
> por lo que ahí conviene probar con EDNS Client Subnet (`dig +subnet`).

### Dominios dinámicos (API Node.js)

Además de `DI_DOMAINS`, el interceptor puede atender los **dominios verificados que se registran en la UI**.
Si `DI_DOMAINS_URL` está definida, un hilo consulta ese endpoint cada `DI_DOMAINS_REFRESH_SECONDS`
(por defecto 30 s) con `Authorization: Bearer <DI_DOMAINS_TOKEN>`. Es el mismo endpoint que usan las
cachés zonales (`GET /api/cache/config`) y de su respuesta solo se lee el campo `name` de cada dominio:

```json
{ "domains": [ { "name": "ejemplo.com", "originUrl": "...", "rules": [ ... ] } ] }
```

- Un dominio del API cubre **el dominio y todos sus subdominios** (`ejemplo.com` y `www.ejemplo.com`).
- Si el API falla (red, HTTP distinto de 200 o JSON inválido) **se conserva la última lista buena**, y se
  avisa una sola vez por cada racha de fallos. Los dominios de `DI_DOMAINS` no se ven afectados.
- Si un dominio se da de baja en la UI, deja de interceptarse en el siguiente refresco.
- Sin `DI_DOMAINS_URL` el comportamiento es el de siempre: solo `DI_DOMAINS`.

### Respaldo de zonas

Si `DI_HEALTH_INTERVAL_SECONDS` es mayor que 0, un hilo intenta cada ese tiempo una conexión TCP a cada caché
zonal (IP de `DI_ZONE_IPS`, puerto `DI_HEALTH_PORT`). Con la zona del país caída:

1. se responde con la zona por defecto (`DI_DEFAULT_ZONE`) si está sana;
2. si tampoco, con la primera zona sana;
3. si ninguna responde se mantiene la zona del país (es mejor que devolver un error).

El desvío queda en la bitácora (`desvio=si`) y los cambios de estado se avisan una vez
(`zona=europa cache=10.0.0.2:80 no responde, se desvía a otra zona`). El sondeo se desactiva con `0`.

### DNS sobre TCP

Además de UDP, atiende **TCP en el mismo puerto** (RFC 7766, mensajes con prefijo de 2 bytes y varias consultas
por conexión). Así un cliente puede reintentar por TCP cuando una respuesta del DNS externo llega truncada.
Las consultas a dominios no interceptados se reenvían al DNS externo por el mismo transporte con que llegaron.
Se desactiva con `DI_TCP=0`. El interceptor limita a 256 las consultas simultáneas y descarta el exceso.

### Tabla país → zona

Está en [`data/country_zones.csv`](data/country_zones.csv) y se incluye en la imagen.

| Zona      | Países                                                      |
| --------- | ----------------------------------------------------------- |
| `america` | Norteamérica, Centroamérica, Caribe y Sudamérica            |
| `europa`  | Europa y África                                             |
| `asia`    | Asia, Medio Oriente y Oceanía                               |

Los nombres de zona deben coincidir con los del chart `zonal-cache` (`america`, `europa`, `asia`).

### Base IP to Country

`data/dbip-country-lite.csv.gz` es la base _IP to Country Lite_ de [db-ip.com](https://db-ip.com)
(septiembre de 2026, licencia CC BY 4.0), la misma del Proyecto 1. El repositorio solo guarda la
versión comprimida; el Dockerfile y `make db` la descomprimen. Al arrancar se cargan en memoria
los ~357 mil rangos IPv4 (unos 4 MB) y cada búsqueda es una búsqueda binaria.

## Variables de entorno

| Variable                 | Por defecto                          | Descripción                                                                  |
| ------------------------ | ------------------------------------ | ---------------------------------------------------------------------------- |
| `DI_ZONE_IPS`            | — (obligatoria)                      | IP de cada caché zonal: `america=10.0.0.1,europa=10.0.0.2,asia=10.0.0.3`    |
| `DI_DOMAINS`             | vacía (no intercepta nada)           | Dominios a interceptar, separados por coma. Admite comodín: `*.cdn.p2.local` |
| `DI_DEFAULT_ZONE`        | `america`                            | Zona para países desconocidos o sin zona. Debe estar en `DI_ZONE_IPS`        |
| `DI_UPSTREAM_DNS`        | `8.8.8.8`                            | DNS externo para los dominios no interceptados                               |
| `DI_UPSTREAM_PORT`       | `53`                                 | Puerto del DNS externo                                                       |
| `DI_UPSTREAM_TIMEOUT_MS` | `3000`                               | Tiempo de espera de la respuesta del DNS externo                             |
| `DI_TTL`                 | `30`                                 | TTL (segundos) de las respuestas redirigidas                                 |
| `DI_PORT`                | `5353`                               | Puerto UDP y TCP en el que escucha (sin privilegios) |
| `DI_COUNTRY_DB`          | `/app/data/dbip-country-lite.csv`    | Ruta de la base IP to Country                                                |
| `DI_ZONES_FILE`          | `/app/data/country_zones.csv`        | Ruta de la tabla país → zona                                                 |
| `DI_TCP`                 | `1`                                  | `1` atiende también DNS sobre TCP; `0` solo UDP                              |
| `DI_DOMAINS_URL`         | vacía (sin dominios dinámicos)       | Endpoint del API con los dominios verificados (`.../api/cache/config`)       |
| `DI_DOMAINS_TOKEN`       | vacía                                | Token Bearer para ese endpoint. En Kubernetes va en un Secret                |
| `DI_DOMAINS_REFRESH_SECONDS` | `30`                             | Cada cuánto se consulta el API                                               |
| `DI_HEALTH_PORT`         | `80`                                 | Puerto TCP de las cachés zonales que se sondea                               |
| `DI_HEALTH_INTERVAL_SECONDS` | `5`                              | Intervalo del sondeo; `0` lo desactiva                                       |
| `DI_HEALTH_TIMEOUT_MS`   | `1000`                               | Tiempo de espera de cada sondeo                                              |

Un comodín `*.dominio` coincide con los subdominios (`img.dominio`) pero no con `dominio`.
Si la configuración es inválida, el proceso termina con un mensaje que indica la variable.

## Ejecución

### Con Docker Compose (desarrollo)

```bash
cd dns-interceptor
docker compose up --build
```

Escucha en el puerto **15353** del equipo (el 5353 lo usa mDNS en macOS). Se pueden cambiar los
valores con variables de entorno, por ejemplo `DI_DOMAINS=mi.dominio docker compose up`.

```bash
# Cliente de Costa Rica, España y Japón (subred simulada con EDNS Client Subnet)
dig +short @127.0.0.1 -p 15353 apache.p2.local +subnet=186.15.0.0/24   # 10.0.0.1 (america)
dig +short @127.0.0.1 -p 15353 apache.p2.local +subnet=80.58.61.0/24    # 10.0.0.2 (europa)
dig +short @127.0.0.1 -p 15353 apache.p2.local +subnet=133.1.1.0/24     # 10.0.0.3 (asia)

# Dominio no interceptado: lo resuelve el DNS externo
dig +short @127.0.0.1 -p 15353 example.com
```

`nslookup` no permite enviar EDNS Client Subnet; con él se usa la IP de origen de la consulta.

### Con Kubernetes y Helm

El chart está en [`deploy/helm/dns-interceptor`](../deploy/helm/dns-interceptor).

```bash
# 1. Construir la imagen (con Docker Desktop el cluster la ve directamente)
docker build -t dns-interceptor:latest dns-interceptor

# 2. Desplegar las cachés zonales (chart zonal-cache, release "zonal-cache")
#    y después el interceptor: toma la IP de cada Service automáticamente.
helm install dns-interceptor deploy/helm/dns-interceptor

# O indicando las IPs a mano
helm install dns-interceptor deploy/helm/dns-interceptor \
  --set zones.america=10.96.0.10 --set zones.europa=10.96.0.11 --set zones.asia=10.96.0.12

# 3. Probar (Service NodePort 30053/UDP)
dig +short @127.0.0.1 -p 30053 apache.p2.local +subnet=80.58.61.0/24
```

Parámetros principales del chart (`values.yaml`): `domains`, `zones`, `defaultZone`,
`zonalCache.release`, `upstream.dns`, `ttl`, `service.type`, `service.nodePort`,
`service.preserveClientIP`, `tcp.enabled`, `domainsSync.*` y `health.*`.

Para tomar los dominios del API (el token queda en un Secret):

```bash
helm upgrade --install dns-interceptor deploy/helm/dns-interceptor \
  --set domainsSync.url=https://<api>.vercel.app/api/cache/config \
  --set domainsSync.token=<ZC_CONFIG_TOKEN>
```

Si el release de la caché zonal tiene otro nombre, indícalo con `--set zonalCache.release=<nombre>`.
Los Services de la caché se llaman `<release>-zonal-cache-<zona>`.

### Diagnóstico

El binario tiene un modo para consultar el país y la zona de una IP sin levantar el servidor:

```bash
docker run --rm -e DI_ZONE_IPS=america=1.1.1.1,europa=2.2.2.2,asia=3.3.3.3 \
  dns-interceptor --lookup 80.58.61.250
# ip=80.58.61.250 pais=ES zona=europa cache=2.2.2.2
```

Cada consulta interceptada deja una línea en la bitácora:

```text
consulta=apache.p2.local cliente=80.58.61.0(ecs) pais=ES zona=europa respuesta=10.0.0.2
consulta=apache.p2.local cliente=80.58.61.0(ecs) pais=ES zona=america respuesta=10.0.0.1 desvio=si   # europa caída
```

## Pruebas

```bash
cd dns-interceptor
make test
```

Cubren el parseo de consultas DNS (incluido EDNS Client Subnet), la construcción de respuestas y de
errores FORMERR/NOTIMP, la búsqueda de países, la tabla de zonas, la validación de la configuración,
la selección de caché con respaldo de zonas, el parser de la respuesta del API, los dominios dinámicos
y el sondeo de salud. Solo requieren `gcc` y `make`.

Para compilar el binario hace falta además `libcurl` (en Debian/Ubuntu: `libcurl4-openssl-dev`; en macOS
viene con las herramientas de línea de comandos). La imagen de Docker ya la incluye.

## Estructura

| Archivo           | Función                                                            |
| ----------------- | ------------------------------------------------------------------ |
| `src/main.c`      | Servidores UDP y TCP, un hilo por consulta, reenvío al DNS externo |
| `src/dns.c`       | Parseo de consultas (RFC 1035, EDNS0) y construcción de respuestas |
| `src/geo.c`       | Base IP to Country y tabla país → zona                             |
| `src/resolver.c`  | Selección de la caché zonal (país → zona → IP, con respaldo) |
| `src/domains.c`   | Parser de la respuesta del API y conjunto de dominios dinámicos    |
| `src/sync.c`      | Hilo que consulta el API de dominios (libcurl)                     |
| `src/health.c`    | Sondeo TCP de las cachés zonales para el respaldo de zonas         |
| `src/config.c`    | Lectura y validación de las variables `DI_*`                       |
| `tests/`          | Pruebas unitarias                                                  |
| `data/`           | Base IP to Country (comprimida) y tabla país → zona                |

## Qué funciona y qué no

| Funciona                                                                           | No funciona / limitaciones                                                           |
| ---------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| Respuestas A con la IP de la caché zonal según el país (probado con `dig +subnet`) | Solo IPv4: no hay registros AAAA ni búsquedas de países para clientes IPv6           |
| Reenvío de dominios no interceptados al DNS externo, por UDP y por TCP             | Sin caché de respuestas del DNS externo: cada consulta se reenvía                    |
| Comodines (`*.dominio`) y zona por defecto                                         | No valida DNSSEC                                                                     |
| Dominios dinámicos desde el API (con refresco y última lista buena si el API cae)  | Se activa con `domainsSync.url` y el token real del API; no se probó en el cluster sin ese token |
| Respaldo de zonas: si una caché no responde se desvía a otra zona sana             | El sondeo es una conexión TCP: no comprueba que la caché sirva contenido             |
| DNS sobre TCP (RFC 7766), FORMERR/NOTIMP y tope de consultas simultáneas            | Con Docker Desktop (un nodo) el cliente llega con la IP del gateway: probar con ECS. Además, el NodePort TCP no llega al equipo: usar `kubectl port-forward svc/dns-interceptor-dns-interceptor 5354:53` |
| Despliegue con Helm y obtención automática de las IPs de las cachés zonales        | El nombre de dominio de las respuestas A se limita a registros de tipo A             |
| Imagen sin privilegios, apagado limpio con SIGTERM                                 |                                                                                      |
| Pruebas unitarias (`make test`)                                                    |                                                                                      |
