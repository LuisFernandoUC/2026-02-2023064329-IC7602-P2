# Guía de pruebas manuales

Pasos para probar el proyecto completo a mano, sin el script `deploy/test-k8s.sh` ni las pruebas unitarias.

Componentes que se prueban:

- **DNS Interceptor**
- **Zonal Cache**, con Apache y con la Rest API Java como servidores de origen
- **API Node.js** y **UI**, publicados en Vercel

La guía tiene dos partes:

| Parte | Qué usa la caché como configuración | Para qué sirve |
| --- | --- | --- |
| **A** | [values-local.yaml](../deploy/helm/zonal-cache/values-local.yaml), un archivo con todo escrito | Probar la caché, el DNS y los orígenes sin depender de Firebase |
| **B** | El API Node.js (`/api/cache/config`), con los datos que se crean en la UI | Probar el sistema completo: UI → API → Firestore → caché |

Cada prueba indica **qué se prueba**, **los pasos**, **qué se debe ver** y **por qué**. Al final hay una tabla para copiar los resultados a la sección 9 del README.

---

## 0. Antes de empezar

### 0.1 Herramientas

| Herramienta | Para qué | Cómo comprobarlo |
| --- | --- | --- |
| Docker Desktop con **Kubernetes activado** (Settings → Kubernetes → Enable Kubernetes) | Cluster local | `kubectl get nodes` muestra un nodo `Ready` |
| Helm 3 | Instalar los charts | `helm version` |
| Git Bash | Ejecutar los comandos de esta guía | — |
| Un navegador | Usar la UI y el login de recursos | — |

### 0.2 Dónde se escribe cada comando

Los bloques de comandos indican dónde ejecutarlos:

- **`# Git Bash`**: en tu equipo, en la raíz del repositorio.
- **`# Pod`**: dentro del pod de pruebas del paso A.2. Las IPs de las cachés son internas del cluster, así que muchas pruebas se hacen desde adentro.
- **`# PowerShell (administrador)`**: solo para editar el archivo `hosts` en la parte B.

### 0.3 Cómo se ve la arquitectura

```text
cliente ──DNS──▶ DNS Interceptor ──▶ "tu caché es la de europa: 10.96.x.x"
cliente ──HTTP/HTTPS (Host: apache.p2.local)──▶ Zonal Cache europa
                                                  ├─ HIT  → responde desde disco
                                                  └─ MISS → pide al origen (Apache / Rest API Java / sitio externo)
Zonal Cache ──cada 30 s──▶ API Node.js (Vercel) ──▶ Firestore     (solo en la parte B)
```

Encabezados que agrega la caché y que se revisan en todas las pruebas:

| Encabezado | Significado |
| --- | --- |
| `X-Cache: MISS` | No estaba en disco: se pidió al origen y se guardó |
| `X-Cache: HIT` | Se respondió desde el disco de la caché, sin tocar el origen |
| `X-Cache: BYPASS` | No se puede guardar (no es GET, la extensión no tiene TTL o no hay regla); se pasó directo al origen |
| `X-Zonal-Cache: europa` | Zona de la caché que respondió |
| `Age: 12` | Segundos que lleva guardada la respuesta (solo en HIT) |

---

# Parte A: caché, DNS y orígenes (sin el API)

## A.1 Construir e instalar

**Qué se hace:** se construyen las 4 imágenes y se instalan los charts en el cluster local.

```bash
# Git Bash
cd ~/Desktop/2026-02-2023064329-IC7602-P2

docker build -t apache-p2 apache
docker build -t rest-api-java rest-api-java
docker build -t zonal-cache zonal-cache
docker build -t dns-interceptor dns-interceptor

helm install apache deploy/helm/apache
helm install rest-api-java deploy/helm/rest-api-java
helm install zonal-cache deploy/helm/zonal-cache -f deploy/helm/zonal-cache/values-local.yaml
kubectl get pods -w      # esperar a que todos estén Running; salir con Ctrl+C
helm install dns-interceptor deploy/helm/dns-interceptor
kubectl get pods,svc
```

**Qué se debe ver:** 6 pods `Running`: apache, rest-api-java, tres zonal-cache (america, europa y asia) y dns-interceptor. También sus Services, con estos nombres:

| Service | Puerto | Qué es |
| --- | --- | --- |
| `apache-apache` | 80 | Origen Apache |
| `rest-api-java-rest-api-java` | 8080 | Origen Rest API Java |
| `zonal-cache-zonal-cache-america` / `-europa` / `-asia` | 80 y 443 | Una caché por zona |
| `dns-interceptor-dns-interceptor` | 53 (NodePort 30053) | DNS |

**Por qué en este orden:** el DNS Interceptor busca, al instalarse, la IP de los Services de las 3 cachés. Si se instala antes, no las encuentra.

Si ya estaba instalado y solo cambió `values-local.yaml`:

```bash
# Git Bash
helm upgrade zonal-cache deploy/helm/zonal-cache -f deploy/helm/zonal-cache/values-local.yaml
```

## A.2 Pod de pruebas

**Qué se hace:** se crea un contenedor dentro del cluster con `curl`, `dig` y `openssl`. Desde ahí se alcanzan las IPs internas de las cachés.

```bash
# Git Bash
kubectl run cliente --rm -it --image=alpine:3.20 --restart=Never -- sh
```

Dentro del pod se instalan las herramientas y se definen tres atajos:

```sh
# Pod
apk add --no-cache curl bind-tools openssl
DNS=dns-interceptor-dns-interceptor
IP=$(dig +short @$DNS apache.p2.local +subnet=80.58.61.0/24)   # IP de la caché europa
echo "Caché europa: $IP"

# r <dominio> <args de curl>: envía la petición a la caché como si el DNS hubiera resuelto el dominio a $IP
r() { curl -sik --resolve "$1:80:$IP" --resolve "$1:443:$IP" "${@:2}"; }
# x: muestra solo los encabezados importantes
x() { grep -iE '^(HTTP|x-cache|x-zonal|location|set-cookie|age|content-type)'; }
```

**Por qué `--resolve`:** es lo que haría un navegador después de preguntarle al DNS Interceptor. Conecta a la IP de la caché y envía `Host: apache.p2.local`. La caché usa ese `Host` para saber qué dominio y qué reglas aplicar.

Si sales del pod (`exit`), se borra. Para volver, repite este paso.

## A.3 Servidores de origen, sin caché (pruebas 1 y 2)

**Qué se prueba:** que Apache y la Rest API Java funcionan solos, antes de poner la caché delante.

```sh
# Pod
curl -si http://apache-apache/ | head -15                        # HTML de Apache
curl -s  http://rest-api-java-rest-api-java:8080/health          # {"status":"ok"}
curl -s  http://rest-api-java-rest-api-java:8080/api/hello
curl -s  -X POST -d '{"name":"uno","description":"primero"}' http://rest-api-java-rest-api-java:8080/api/items
curl -s  http://rest-api-java-rest-api-java:8080/api/items
curl -s  -X PUT -d '{"name":"uno-mod"}' http://rest-api-java-rest-api-java:8080/api/items/1
curl -si -X DELETE http://rest-api-java-rest-api-java:8080/api/items/1 | head -1   # 204
```

**Qué se debe ver:** el HTML de Apache, y las respuestas JSON de la API Java con 201 al crear, 200 al modificar y 204 al borrar. Ninguna respuesta trae `X-Cache`, porque no pasaron por la caché.

## A.4 DNS Interceptor y geolocalización (prueba 5)

**Qué se prueba:** el DNS responde con la IP de la caché según el país del cliente.

```sh
# Pod
dig +short @$DNS apache.p2.local +subnet=8.8.8.0/24       # Estados Unidos -> IP de america
dig +short @$DNS apache.p2.local +subnet=80.58.61.0/24    # España         -> IP de europa
dig +short @$DNS apache.p2.local +subnet=1.0.16.0/24      # Japón          -> IP de asia
dig +short @$DNS api.cdn.p2.local +subnet=80.58.61.0/24   # comodín *.cdn.p2.local -> europa
nslookup apache.p2.local $DNS                              # sin subred -> zona por defecto (america)
dig +short @$DNS google.com                                # dominio no interceptado -> IP real de Google
dig +short +tcp @$DNS apache.p2.local +subnet=1.0.16.0/24 # la misma consulta por TCP
```

Compara las IPs con las de `kubectl get svc` (en Git Bash).

**Qué se debe ver:**
- Las tres primeras consultas devuelven tres IPs distintas: las de las cachés `america`, `europa` y `asia`.
- `google.com` devuelve una IP pública.

**Por qué `+subnet`:** todas las pruebas salen del mismo pod, con una IP privada. La opción EDNS Client Subnet le dice al DNS de qué país es el cliente, igual que hacen los resolvers públicos. `nslookup` no envía esta opción, por eso siempre recibe la zona por defecto.

Desde Windows también se puede usar el NodePort:

```bash
# Git Bash o PowerShell
nslookup -port=30053 apache.p2.local 127.0.0.1
```

## A.5 Zonal Cache con Apache (prueba 2 y SSL termination)

**Qué se prueba:** que la caché guarda en disco, responde desde ahí y que cada zona tiene su propia caché.

```sh
# Pod
r apache.p2.local http://apache.p2.local/?prueba=1 | x    # 1.ª vez
r apache.p2.local http://apache.p2.local/?prueba=1 | x    # 2.ª vez
```

**Qué se debe ver:** la primera vez `X-Cache: MISS`; la segunda `X-Cache: HIT` con `Age: <segundos>`. En ambas, `X-Zonal-Cache: europa`.

Para ver que cada zona es independiente, se repite la misma URL contra la caché de asia:

```sh
# Pod
IPASIA=$(dig +short @$DNS apache.p2.local +subnet=1.0.16.0/24)
curl -si --resolve apache.p2.local:80:$IPASIA http://apache.p2.local/?prueba=1 | x   # MISS, zona asia
```

**Por qué:** cada zona tiene su propio disco. Que europa ya tenga la página no significa que asia la tenga.

**SSL termination:** el cliente habla HTTPS con la caché, y la caché habla HTTP con Apache.

```sh
# Pod
r apache.p2.local https://apache.p2.local/?prueba=2 | x    # MISS por HTTPS
r apache.p2.local https://apache.p2.local/?prueba=2 | x    # HIT
echo | openssl s_client -connect $IP:443 -servername apache.p2.local 2>/dev/null | grep -E 'subject=|issuer='
```

`-k` (incluido en `r`) acepta el certificado autofirmado que genera el chart.

## A.6 Zonal Cache con la Rest API Java (pruebas 1 y 6)

**Qué se prueba:** la caché delante de una API con los cuatro verbos y protegida con `x-api-key`.

Reglas en `values-local.yaml` para `api.cdn.p2.local`:

| Ruta | Acceso | TTL | Política |
| --- | --- | --- | --- |
| `/api/hello` | Público | 30 s | FIFO |
| `/api/items*` | `x-api-key` (la clave de prueba es `clave-valida`) | 10 s | LFU |

```sh
# Pod
H=api.cdn.p2.local; K="x-api-key: clave-valida"

r $H http://$H/api/hello | x                        # MISS
r $H http://$H/api/hello | x                        # HIT

r $H http://$H/api/items | x                        # 401: falta la clave
r $H -H "x-api-key: incorrecta" http://$H/api/items | x   # 401: clave inválida
r $H -H "$K" http://$H/api/items | x                # 200, MISS
r $H -H "$K" http://$H/api/items | x                # 200, HIT

r $H -H "$K" -X POST -d '{"name":"dos","description":"por la caché"}' http://$H/api/items   # 201, BYPASS; anota el "id"
r $H -H "$K" -X PUT -d '{"name":"dos-mod"}' http://$H/api/items/<id> | x    # 200, BYPASS
r $H -H "$K" -X DELETE http://$H/api/items/<id> | x                         # 204, BYPASS
r $H -H "$K" http://$H/api/items/<id> | x                                   # 404
```

**Qué se debe ver:**
- La clave nunca llega al origen: la caché quita `x-api-key` antes de reenviar.
- POST, PUT y DELETE siempre dan `BYPASS`, porque solo se guardan los GET.

**Limitación que conviene anotar:** después del POST, `GET /api/items` puede seguir mostrando la lista vieja (HIT) hasta que vence su TTL de 10 s. La caché no invalida automáticamente cuando cambian los datos.

## A.7 Sitios externos por HTTP y HTTPS (pruebas 3 y 4)

**Qué se prueba:** la caché delante de un sitio de Internet. `values-local.yaml` tiene dos dominios cuyo origen es `example.com`.

```sh
# Pod
r externo-http.cdn.p2.local  http://externo-http.cdn.p2.local/ | x       # MISS (caché -> http://example.com)
r externo-http.cdn.p2.local  http://externo-http.cdn.p2.local/ | x       # HIT
r externo-http.cdn.p2.local  http://externo-http.cdn.p2.local/ | grep -o '<title>.*</title>'   # Example Domain

r externo-https.cdn.p2.local http://externo-https.cdn.p2.local/ | x      # MISS (caché -> https://example.com)
r externo-https.cdn.p2.local https://externo-https.cdn.p2.local/ | x     # HIT, HTTPS de punta a punta
```

**Por qué:** en el segundo dominio, la caché valida el certificado real de `example.com` al conectarse al origen (`origin.verifyTls: true`). Además, el cliente le habla HTTPS a la caché.

Si da `502`, el cluster no tiene salida a Internet o el DNS del cluster no resuelve `example.com`.

## A.8 TTL y qué se guarda

**Qué se prueba:** que una respuesta vence según el TTL de su tipo de archivo y que no todo se guarda.

```sh
# Pod
r ttl.cdn.p2.local http://ttl.cdn.p2.local/api/hello | x    # MISS
r ttl.cdn.p2.local http://ttl.cdn.p2.local/api/hello | x    # HIT
sleep 6
r ttl.cdn.p2.local http://ttl.cdn.p2.local/api/hello | x    # MISS: venció el TTL de 5 s

r apache.p2.local http://apache.p2.local/archivo.txt | x    # 404 y BYPASS: .txt no tiene TTL en la regla
r apache.p2.local http://apache.p2.local/nada.html | x      # 404 y MISS
r apache.p2.local http://apache.p2.local/nada.html | x      # MISS otra vez: los errores no se guardan
```

**Por qué:** la regla de Apache solo define TTL para `html`, `css` y `png`. Las rutas sin extensión, como `/`, cuentan como `html`. Además, solo se guardan respuestas `200`.

## A.9 Algoritmos de reemplazo (prueba 8)

**Qué se prueba:** que cuando no hay espacio, la caché expulsa el objeto que indica la política.

**Montaje:**
- Hay un dominio por política: `lru.cdn.p2.local`, `lfu…`, `fifo…`, `mru…` y `random…`.
- Cada uno tiene 350 B de espacio, y cada respuesta de `/api/hello` ocupa 107 B en disco, así que **caben 3**.
- Al guardar el 4.º objeto, la caché tiene que expulsar uno.
- Las URLs se diferencian por `?k=1`, `?k=2`, etc.

Para saber qué se expulsó, se pide cada objeto otra vez. HIT significa que sigue en disco; MISS, que fue expulsado. Se comprueban primero los que deben seguir, porque un HIT no expulsa nada, y al final la víctima.

```sh
# Pod
g() { printf '%s k=%s -> ' "$1" "$2"; r $1 "http://$1/api/hello?k=$2" | grep -i x-cache | tr -d '\r'; }
```

**LRU (sale el usado hace más tiempo):**

```sh
H=lru.cdn.p2.local
g $H 1; g $H 2; g $H 3        # 3 MISS: se guardan 1, 2 y 3
g $H 1                         # HIT: el 1 pasa a ser el más reciente
g $H 4                         # MISS: no hay espacio; sale el menos reciente, el 2
g $H 1; g $H 3; g $H 4         # HIT, HIT, HIT
g $H 2                         # MISS: el 2 fue expulsado
```

**LFU (sale el usado menos veces):**

```sh
H=lfu.cdn.p2.local
g $H 1; g $H 2; g $H 3                  # se guardan
g $H 1; g $H 1; g $H 2; g $H 2; g $H 3  # usos: 1→3, 2→3, 3→2
g $H 4                                  # sale el 3 (menos usos), aunque fue el último leído
g $H 1; g $H 2; g $H 4                  # HIT
g $H 3                                  # MISS
```

**FIFO (sale el primero que entró):**

```sh
H=fifo.cdn.p2.local
g $H 1; g $H 2; g $H 3
g $H 1; g $H 1                 # el 1 es el más usado...
g $H 4                         # ...pero sale igual, porque entró primero
g $H 2; g $H 3; g $H 4         # HIT
g $H 1                         # MISS
```

**MRU (sale el usado más recientemente):**

```sh
H=mru.cdn.p2.local
g $H 1; g $H 2; g $H 3
g $H 2                         # el 2 es ahora el más reciente
g $H 4                         # sale el 2
g $H 1; g $H 3; g $H 4         # HIT
g $H 2                         # MISS
```

**Random (sale uno al azar):**

```sh
H=random.cdn.p2.local
g $H 1; g $H 2; g $H 3; g $H 4
g $H 4                         # HIT: el último siempre está
g $H 1; g $H 2; g $H 3         # el primer MISS es la víctima; detente ahí (ese MISS expulsa otro)
```

**Importante:** los objetos quedan guardados 10 minutos. Para repetir una prueba, usa otras claves (`k=11`, `12`, `13`, `14`) o reinstala la caché con `helm upgrade`. La caché se vacía al reiniciar.

## A.10 Límite de tamaño por dominio (prueba 9)

**Qué se prueba:** el límite del dominio se aplica aunque cada URL tenga espacio propio.

**Montaje:**
- `limite.cdn.p2.local` tiene **440 B** en total.
- `/api/hello` (107 B por objeto) y `/health` (80 B por objeto) tienen 350 B cada una.

```sh
# Pod
l() { printf '%s k=%s -> ' "$1" "$2"; r limite.cdn.p2.local "http://limite.cdn.p2.local$1?k=$2" | grep -i x-cache | tr -d '\r'; }
l /api/hello 1; l /api/hello 2; l /api/hello 3   # 321 B
l /health 1                                       # 401 B: todavía cabe
l /health 2                                       # 481 B > 440 B: hay que expulsar algo
l /api/hello 1; l /api/hello 2; l /api/hello 3; l /health 2   # HIT
l /health 1                                       # MISS
```

**Por qué:** con el 2.º `/health`, el dominio habría pasado de 440 B, aunque `/health` solo usaba 160 de sus 350 B. La caché expulsó `/health k=1`: libera espacio primero en la URL que está guardando y, si esa está vacía, en la URL del dominio que más ocupa.

## A.11 Errores de la caché y respaldo de zonas

```sh
# Pod
curl -si -H "Host: no-administrado.com" http://$IP/ | x               # 404: dominio no configurado
r apache.p2.local -H "X-Zonal-Cache-Hop: 1" http://apache.p2.local/ | x   # 508: bucle (la petición ya pasó por una caché)
curl -s http://$IP/__zc/health                                         # ok
```

**Respaldo de zonas:** si una caché cae, el DNS envía a sus clientes a otra zona.

```bash
# Git Bash
kubectl scale deploy/zonal-cache-zonal-cache-europa --replicas=0
```

```sh
# Pod (esperar unos 10 s)
dig +short @$DNS apache.p2.local +subnet=80.58.61.0/24   # ya no es la IP de europa, sino la de otra zona sana
```

```bash
# Git Bash
kubectl scale deploy/zonal-cache-zonal-cache-europa --replicas=1
```

Unos segundos después, la misma consulta vuelve a dar la IP de europa.

**Logs útiles:**

```bash
# Git Bash
kubectl logs deploy/zonal-cache-zonal-cache-europa --tail=30
kubectl logs deploy/dns-interceptor-dns-interceptor --tail=30
```

---

# Parte B: sistema completo con la UI, el API y Firebase

En esta parte la caché deja de usar `values-local.yaml`. Toda la configuración se crea en la UI, se guarda en Firestore y la caché la lee del API cada 30 s.

| Pieza | Dirección |
| --- | --- |
| UI | https://zonal-cache-ui.vercel.app |
| API | https://zonal-cache-api.vercel.app |
| Firestore | Proyecto `IC7602-P2` en la consola de Firebase |

## B.1 Obtener los dos secretos compartidos

**Qué se hace:** la caché y el API tienen que compartir dos valores. Están en Vercel, en el proyecto del API → Settings → Environment Variables. Pídeselos a Nicole o revísalos ahí.

| En Vercel (API) | En la caché (Helm) | Para qué |
| --- | --- | --- |
| `CACHE_API_TOKEN` | `secrets.configToken` | La caché se identifica para descargar la configuración |
| `CACHE_SESSION_SECRET` (al menos 32 caracteres) | `secrets.sessionSecret` | La caché valida los tokens de sesión que firma el API |

Si no existen, se generan dos valores con `openssl rand -hex 32` y se guardan en Vercel. Después hay que volver a desplegar (Deployments → Redeploy), porque Vercel solo lee las variables al desplegar.

**Comprobar que el API entrega la configuración:**

```bash
# Git Bash
TOKEN=<CACHE_API_TOKEN>
curl -s https://zonal-cache-api.vercel.app/api/health
curl -s -H "Authorization: Bearer $TOKEN" https://zonal-cache-api.vercel.app/api/cache/config
```

**Qué se debe ver:** `{"status":"ok"}`, y luego `{"domains":[...]}`. La lista está vacía si todavía no hay dominios verificados. Un `401` significa que el token no coincide; un `503`, que falta configurar `CACHE_API_TOKEN` en Vercel.

## B.2 Conectar la caché al API

```bash
# Git Bash
helm upgrade zonal-cache deploy/helm/zonal-cache \
  --set config.url=https://zonal-cache-api.vercel.app/api/cache/config \
  --set secrets.configToken=<CACHE_API_TOKEN> \
  --set secrets.sessionSecret=<CACHE_SESSION_SECRET> \
  --set loginUrl='https://zonal-cache-ui.vercel.app/#/acceso-recurso'

kubectl rollout status deploy/zonal-cache-zonal-cache-america
kubectl logs deploy/zonal-cache-zonal-cache-america --tail=10
```

**Importante:** este comando **no** lleva `-f values-local.yaml`. Así se elimina la configuración escrita en el archivo y la caché pasa a leer la URL.

**Qué se debe ver en el log:** `configuración inicial cargada`. Si aparece `no se pudo refrescar la configuración`, revisa la URL y el token del paso B.1.

**Opcional:** que el DNS Interceptor también tome los dominios del API.

```bash
# Git Bash
helm upgrade dns-interceptor deploy/helm/dns-interceptor \
  --set domainsSync.url=https://zonal-cache-api.vercel.app/api/cache/config \
  --set domainsSync.token=<CACHE_API_TOKEN>
```

Los dominios de esta guía (`apache.p2.local` y `*.cdn.p2.local`) ya están en la lista fija del DNS, así que este paso solo hace falta con dominios nuevos.

## B.3 Crear una cuenta en la UI

1. Abre https://zonal-cache-ui.vercel.app.
2. Haz clic en **Crear cuenta**, escribe un correo y una contraseña de al menos 6 caracteres y confirma.
3. Entras a **Mis dominios**, que empieza vacío ("Todavía no tienes dominios").

**Qué pasa por detrás:** Firebase Authentication crea la cuenta. Cada petición de la UI al API lleva el ID token, y el API solo muestra los dominios de esa cuenta.

Prueba también **Cerrar sesión** y vuelve a entrar con **Iniciar sesión**. Con una contraseña incorrecta debe aparecer "Correo o contraseña incorrectos".

## B.4 Agregar los dominios

En **Mis dominios** → **Agregar dominio**, crea estos tres:

| Nombre del dominio | Servidor de origen | Límite total de caché |
| --- | --- | --- |
| `apache.p2.local` | `http://apache-apache` | `10` MB |
| `api.cdn.p2.local` | `http://rest-api-java-rest-api-java:8080` | `5` MB |
| `privado.cdn.p2.local` | `http://apache-apache` | `5` MB |

**Qué se debe ver:** el mensaje "Dominio guardado. Falta verificar su propiedad." y el dominio en la tabla, en estado pendiente.

**Por qué esos orígenes:** la caché corre dentro del cluster y resuelve los nombres de los Services. `apache-apache` es el Service del chart de Apache; `rest-api-java-rest-api-java:8080`, el de la API Java.

Errores que conviene probar: un dominio con `http://` o con una ruta, un origen con ruta (`http://apache-apache/x`) y un límite de 0. La UI y el API los rechazan.

## B.5 Verificar los dominios

**Cómo funciona normalmente:**
1. **Verificar** muestra un registro TXT: nombre `_verificacion.<dominio>` y un valor único.
2. Se crea ese registro en el proveedor de DNS del dominio.
3. Se vuelve a hacer clic en **Verificar dominio**, y el API consulta el DNS público.

Solo los dominios **verificados** se envían a la caché.

**Problema con los dominios de prueba:** `*.p2.local` no existe en el DNS público, así que no se le puede crear un TXT. Si haces clic en **Verificar dominio**, verás "El registro TXT todavía no aparece". Eso confirma que la verificación funciona, aunque aquí no pueda completarse.

**Solución para la demo:** marcar el dominio como verificado a mano en Firestore.

1. Abre la [consola de Firebase](https://console.firebase.google.com/) → proyecto `IC7602-P2` → **Firestore Database**.
2. Abre la colección `domains`. Cada documento se llama `<uid>_<dominio>`, por ejemplo `AbC123..._apache.p2.local`.
3. En cada uno de los tres documentos, cambia el campo `verified` de `false` a `true`.
4. Recarga la UI: el estado cambia a **Verificado**.

**Alternativa real:** si tienes un dominio propio, por ejemplo `pruebas.midominio.com`, crea el TXT en su proveedor y verifícalo desde la UI. Ese dominio también debe agregarse al DNS Interceptor.

> Anotar en el README (Estado / Limitaciones) que los dominios `.local` de la demo se verificaron a mano en Firestore.

## B.6 Crear las reglas de URL

Entra a cada dominio con **Abrir** → **Agregar regla**. Cada regla tiene:
- **Ruta o patrón**: con comodines `*` y `?`. Si varias reglas coinciden, gana la más específica.
- **Tamaño máximo de caché (MB)**: no puede superar el del dominio.
- **Tipos de archivo**: extensión, TTL en segundos y política de reemplazo. `*` cubre las extensiones que no tienen fila propia.
- **Mecanismo de acceso**: sin autenticación, API key, o usuario y contraseña.

### `apache.p2.local`

| Ruta | Tamaño | Tipos (extensión · TTL · política) | Acceso |
| --- | --- | --- | --- |
| `/*` | 5 MB | `html` · 60 · LRU, `css` · 300 · LFU, `png` · 3600 · FIFO | Sin autenticación |

### `api.cdn.p2.local`

| Ruta | Tamaño | Tipos | Acceso |
| --- | --- | --- | --- |
| `/api/hello` | 1 MB | `*` · 30 · FIFO | Sin autenticación |
| `/api/items*` | 1 MB | `*` · 10 · LFU | API key |

Luego crea la clave:
1. **Editar** la regla `/api/items*` → **+ Crear clave**.
2. **Copia la clave ahora**, porque no se vuelve a mostrar. La guardaremos como `$KEY`.

**Qué pasa por detrás:** el API guarda solo el SHA-256 de la clave. La caché recibe ese hash y lo compara con el hash de la clave que envía el cliente.

### `privado.cdn.p2.local`

| Ruta | Tamaño | Tipos | Acceso |
| --- | --- | --- | --- |
| `/*` | 2 MB | `html` · 60 · LRU | Usuario y contraseña |

Luego **Editar** la regla → **+ Agregar usuario** → usuario `ana`, contraseña `Clave-Segura-1` → **Agregar usuario**.

**Qué pasa por detrás:** el API guarda el usuario con la contraseña cifrada con scrypt. La contraseña nunca se envía a la caché.

### Comprobar que la caché recibió todo

Espera hasta 30 s, que es el ciclo de recarga, y consulta:

```bash
# Git Bash
curl -s -H "Authorization: Bearer $TOKEN" https://zonal-cache-api.vercel.app/api/cache/config
```

Deben aparecer los tres dominios con sus reglas; la regla de API key trae un hash en `apiKeys`. Con `ZC_LOG_LEVEL=debug` se vería cada recarga en `kubectl logs`.

## B.7 Probar Apache y la Rest API Java con la configuración de la UI

Desde el pod de pruebas (paso A.2):

```sh
# Pod
r apache.p2.local http://apache.p2.local/?b=1 | x           # MISS
r apache.p2.local http://apache.p2.local/?b=1 | x           # HIT

KEY=<la clave copiada en B.6>
H=api.cdn.p2.local
r $H http://$H/api/hello | x                               # MISS, luego HIT
r $H http://$H/api/items | x                               # 401
r $H -H "x-api-key: $KEY" http://$H/api/items | x          # 200
r $H -H "x-api-key: $KEY" -X POST -d '{"name":"desde-ui"}' http://$H/api/items | x   # 201, BYPASS
```

**Cambios desde la UI que llegan a la caché.** Haz cada cambio, espera 30 s y repite la petición:

| Cambio en la UI | Petición | Resultado esperado |
| --- | --- | --- |
| Eliminar la clave de `/api/items*` | `r $H -H "x-api-key: $KEY" http://$H/api/items` | `401` |
| Crear una clave nueva | La misma petición, con la clave nueva | `200` |
| Editar la regla `/*` de Apache y poner TTL `0` en `html` | `r apache.p2.local http://apache.p2.local/?b=2` (dos veces) | `BYPASS` las dos veces (TTL 0 significa no guardar) |
| Eliminar la regla `/api/hello` | `r $H http://$H/api/hello` | `BYPASS`: sin regla, se pasa directo al origen |
| Eliminar el dominio `api.cdn.p2.local` | `r $H http://$H/api/hello` | `404 Dominio no administrado por esta caché` |

Al terminar, vuelve a crear lo que borraste o deja el TTL en 60.

## B.8 Login con usuario y contraseña desde el navegador (prueba 7)

**Qué se prueba:** el flujo completo. El navegador pide un recurso protegido, la caché lo envía a la UI, la UI valida con el API y el navegador vuelve a la caché con una sesión.

**Problema:** el navegador de Windows no alcanza las IPs internas del cluster. **Solución:** abrir un túnel a una caché y hacer que `privado.cdn.p2.local` apunte a tu propio equipo.

1. Abre el túnel y deja esa terminal abierta:

   ```bash
   # Git Bash (terminal aparte)
   kubectl port-forward svc/zonal-cache-zonal-cache-america 8080:80 8443:443
   ```

2. Haz que el nombre apunte a tu equipo:

   ```powershell
   # PowerShell (administrador)
   Add-Content C:\Windows\System32\drivers\etc\hosts "127.0.0.1 privado.cdn.p2.local apache.p2.local api.cdn.p2.local"
   ```

3. En el navegador abre **http://privado.cdn.p2.local:8080/**. Conviene una ventana de incógnito y las herramientas de desarrollador (F12 → Red) abiertas.

**Qué se debe ver:**

| Paso | Qué ocurre | Qué mirar |
| --- | --- | --- |
| 1 | La caché responde `302` hacia `https://zonal-cache-ui.vercel.app/#/acceso-recurso?domain=privado.cdn.p2.local&path=/&callback=...` | En F12 → Red, la primera petición y su `Location` |
| 2 | La UI muestra "Acceder al recurso" con el **Dominio** y la **Ruta solicitada** | Que se vean `privado.cdn.p2.local` y `/` |
| 3 | Escribe `ana` y una contraseña incorrecta | "Usuario o contraseña incorrectos." |
| 4 | Escribe `ana` / `Clave-Segura-1` → **Acceder** | El navegador vuelve a `http://privado.cdn.p2.local:8080/__zc/auth?next=/&token=...` |
| 5 | La caché crea la sesión y redirige a `/` | F12 → Aplicación → Cookies: `zc_session` (HttpOnly) |
| 6 | Se muestra la página de Apache | En la respuesta: `X-Cache: MISS` y luego, al recargar, `HIT`; `X-Zonal-Cache: america` |

**Pruebas adicionales:**

- **Cerrar sesión:** abre http://privado.cdn.p2.local:8080/__zc/logout?next=/. Se borra la cookie y vuelve a pedir login. La UI también tiene el enlace "Cerrar sesión del recurso".
- **Sesión por regla:** agrega una segunda regla `/otra/*` con usuario y contraseña y otro usuario. La sesión de `ana` no sirve para `/otra/...`: vuelve a pedir login, porque cada token está ligado a su regla.
- **Usuario borrado:** en la UI, elimina a `ana`. Su sesión actual sigue valiendo hasta que vence (`CACHE_SESSION_SECONDS`, 1 h), pero un login nuevo falla. Anotarlo como limitación: no hay revocación inmediata.
- **HTTPS:** https://privado.cdn.p2.local:8443/ funciona igual, después de aceptar el aviso del certificado autofirmado. La cookie lleva además `Secure`.
- **Desde la terminal, sin navegador:**

  ```bash
  # Git Bash
  curl -si -H "Host: privado.cdn.p2.local" http://127.0.0.1:8080/ | grep -iE '^(HTTP|location)'   # 302 al login
  ```

Con el mismo túnel también se puede probar Apache y la API Java desde Windows:

```bash
# Git Bash
curl -si http://apache.p2.local:8080/ | grep -iE '^(HTTP|x-cache|x-zonal)'
curl -si -H "x-api-key: $KEY" http://api.cdn.p2.local:8080/api/items | grep -iE '^(HTTP|x-cache)'
```

Al terminar, borra la línea que agregaste a `hosts` y cierra el túnel con Ctrl+C.

---

## C. Limpiar

```bash
# Git Bash
helm uninstall dns-interceptor zonal-cache rest-api-java apache
kubectl delete pod cliente --ignore-not-found
```

Los datos de la UI quedan en Firestore. Para borrarlos, elimina los dominios desde **Mis dominios**: se borran también sus reglas, claves y usuarios.

---

## D. Resultados para el README (sección 9)

| # | Prueba | Dónde | Resultado esperado |
| --- | --- | --- | --- |
| 1 | Rest API Java | A.3, A.6, B.7 | Los 4 verbos funcionan directo y a través de la caché; GET MISS→HIT; POST, PUT y DELETE con BYPASS |
| 2 | Apache con HTML simple | A.3, A.5, B.7 | MISS→HIT con `Age`; zonas independientes; HTTPS con SSL termination |
| 3 | Sitio externo HTTP | A.7 | `http://example.com` MISS→HIT |
| 4 | Sitio externo HTTPS | A.7 | `https://example.com` como origen; HTTPS de punta a punta |
| 5 | DNS con `nslookup` y geolocalización | A.4, A.11 | Estados Unidos→america, España→europa, Japón→asia; los dominios no interceptados se reenvían; respaldo si cae una zona |
| 6 | `x-api-key` | A.6, B.6, B.7 | 401 sin clave o con clave incorrecta; 200 con clave válida; una clave borrada en la UI deja de servir en ≤30 s |
| 7 | Usuario y contraseña | B.8 | Redirección a la UI, login, cookie `zc_session`, acceso, logout; sesión limitada a su regla |
| 8 | Algoritmos de reemplazo | A.9 | LRU sale 2; LFU sale 3; FIFO sale 1; MRU sale 2; Random sale uno al azar |
| 9 | Límite por dominio | A.10 | Al pasar 440 B se expulsa `/health k=1` aunque su URL tenía espacio |
| — | TTL | A.8 | Vence a los 5 s; las extensiones sin TTL y las respuestas 404 no se guardan |
| — | UI y API | B.3 a B.7 | Cuentas, dominios, verificación TXT, reglas, claves y usuarios; los cambios llegan a la caché en ≤30 s |
