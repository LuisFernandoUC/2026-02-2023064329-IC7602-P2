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

1. Recibe la consulta por UDP y valida que sea una consulta estándar.
2. Si el dominio no está en `DI_DOMAINS`, la reenvía tal cual al DNS externo y devuelve su respuesta.
3. Si está, determina el país de origen y lo traduce a una zona (`america`, `europa` o `asia`).
4. Responde con la IP de la caché zonal de esa zona (registro A, TTL de `DI_TTL` segundos).

**País desconocido:** si la IP no está en la base (por ejemplo una IP privada) o su país no tiene
zona, se usa la zona por defecto (`DI_DEFAULT_ZONE`).

**Consultas que no son tipo A** (por ejemplo AAAA) sobre un dominio interceptado se responden
con éxito y sin registros, para que el cliente use IPv4.

### ¿De dónde sale la IP del cliente?

Se toma, en este orden:

1. La subred indicada en la opción **EDNS Client Subnet** (RFC 7871) si la consulta la trae.
   Es lo que usan los resolvers públicos y permite probar zonas distintas con `dig +subnet`.
2. La IP de origen del paquete UDP.

> En Kubernetes, un Service `NodePort` oculta la IP real del cliente (el paquete llega con la IP
> del nodo o del gateway), por lo que sin EDNS se usaría la zona por defecto. Para que el país se
> detecte con la IP real hay que publicar el Service como `LoadBalancer` con
> `externalTrafficPolicy: Local`, o usar EDNS Client Subnet.

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
| `DI_PORT`                | `5353`                               | Puerto UDP en el que escucha (sin privilegios)                               |
| `DI_COUNTRY_DB`          | `/app/data/dbip-country-lite.csv`    | Ruta de la base IP to Country                                                |
| `DI_ZONES_FILE`          | `/app/data/country_zones.csv`        | Ruta de la tabla país → zona                                                 |

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
`zonalCache.release`, `upstream.dns`, `ttl`, `service.type` y `service.nodePort`.

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
```

## Pruebas

```bash
cd dns-interceptor
make test
```

Cubren el parseo de consultas DNS (incluido EDNS Client Subnet), la construcción de respuestas,
la búsqueda de países, la tabla de zonas, la validación de la configuración y la selección de
caché. Solo requieren `gcc` y `make`.

## Estructura

| Archivo           | Función                                                            |
| ----------------- | ------------------------------------------------------------------ |
| `src/main.c`      | Servidor UDP, un hilo por consulta, reenvío al DNS externo         |
| `src/dns.c`       | Parseo de consultas (RFC 1035, EDNS0) y construcción de respuestas |
| `src/geo.c`       | Base IP to Country y tabla país → zona                             |
| `src/resolver.c`  | Selección de la caché zonal (país → zona → IP)                     |
| `src/config.c`    | Lectura y validación de las variables `DI_*`                       |
| `tests/`          | Pruebas unitarias                                                  |
| `data/`           | Base IP to Country (comprimida) y tabla país → zona                |

## Qué funciona y qué no

| Funciona                                                                           | No funciona / limitaciones                                                           |
| ---------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| Respuestas A con la IP de la caché zonal según el país (probado con `dig +subnet`) | Solo IPv4: no hay registros AAAA ni búsquedas de países para clientes IPv6           |
| Reenvío de dominios no interceptados al DNS externo                                | Solo DNS sobre UDP: no atiende consultas por TCP ni respuestas truncadas             |
| Comodines (`*.dominio`) y zona por defecto                                         | Un Service `NodePort` oculta la IP real del cliente (ver arriba)                     |
| Despliegue con Helm y obtención automática de las IPs de las cachés zonales        | No comprueba si la caché zonal está disponible (no hay _health checks_)              |
| Imagen sin privilegios, apagado limpio con SIGTERM                                 | Sin caché de respuestas del DNS externo: cada consulta se reenvía                    |
| Pruebas unitarias (`make test`)                                                    | No valida DNSSEC                                                                     |
