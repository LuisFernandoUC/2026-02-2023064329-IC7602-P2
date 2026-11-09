#!/usr/bin/env bash
#
# test-k8s.sh - Pruebas de integración del Proyecto 2 sobre Kubernetes.
#
# Requiere los cuatro charts instalados (ver Docs/Integracion.md, sección 8):
#   helm install apache          deploy/helm/apache
#   helm install rest-api-java   deploy/helm/rest-api-java
#   helm install zonal-cache     deploy/helm/zonal-cache -f deploy/helm/zonal-cache/values-local.yaml
#   helm install dns-interceptor deploy/helm/dns-interceptor
#
# Crea un pod temporal (alpine + curl + dig) dentro del cluster, porque las IPs de
# las cachés son ClusterIP y solo se alcanzan desde adentro. Desde ese pod prueba:
#   1. Orígenes directos (Apache y Rest API Java).
#   2. DNS Interceptor: zona según el país (EDNS Client Subnet), comodín, reenvío, TCP.
#   3. Zonal Cache en cada zona: health, MISS -> HIT, independencia entre zonas.
#   4. SSL termination (HTTPS).
#   5. Rest API Java a través de la caché: x-api-key, GET/POST/PUT/DELETE.
#   6. Respuestas de error propias de la caché (404 dominio, 508 bucle).
#   7. Sitios externos por HTTP y HTTPS (example.com como origen).
#   8. TTL y qué se guarda: vencimiento, tipos sin TTL, respuestas que no son 200.
#   9. Algoritmos de reemplazo en vivo: LRU, LFU, FIFO, MRU y Random.
#  10. Límite de tamaño por dominio.
#  11. Usuario y contraseña: redirección al login, callback, cookie, Bearer, logout y
#      sesiones inválidas (los JWT se firman en el pod con el secreto de values-local.yaml).
#  12. (opcional, --unit) Pruebas unitarias de la caché y del DNS Interceptor.
#  13. (opcional, --failover) Respaldo de zonas: apaga la caché europa y comprueba que el DNS
#      envíe a sus clientes a otra zona sana; después la vuelve a encender.
#
# Uso:
#   deploy/test-k8s.sh [-n namespace] [--unit] [--failover] [--keep] [-h]
#
# Variables de entorno (por si los releases tienen otros nombres):
#   APACHE_RELEASE, REST_RELEASE, CACHE_RELEASE, DNS_RELEASE, ZONES, API_KEY, SESSION_SECRET,
#   LOGIN_URL
#
set -uo pipefail

NAMESPACE="default"
RUN_UNIT=0
RUN_FAILOVER=0
KEEP_POD=0

APACHE_RELEASE="${APACHE_RELEASE:-apache}"
REST_RELEASE="${REST_RELEASE:-rest-api-java}"
CACHE_RELEASE="${CACHE_RELEASE:-zonal-cache}"
DNS_RELEASE="${DNS_RELEASE:-dns-interceptor}"
ZONES="${ZONES:-america europa asia}"
API_KEY="${API_KEY:-clave-valida}"   # su SHA-256 está en values-local.yaml
SESSION_SECRET="${SESSION_SECRET:-secreto-local-de-pruebas}"    # secrets.sessionSecret
LOGIN_URL="${LOGIN_URL:-https://ui.p2.local/#/acceso-recurso}"  # loginUrl

APACHE_HOST="apache.p2.local"
API_HOST="api.cdn.p2.local"
POD="p2-tester"
TESTER_IMAGE="alpine:3.20"

# Subredes de prueba para EDNS Client Subnet (país -> zona en country_zones.csv)
declare -A SUBNET_ZONE=(
    ["8.8.8.0/24"]="america"     # Estados Unidos
    ["80.58.61.0/24"]="europa"   # España
    ["1.0.16.0/24"]="asia"       # Japón
)

usage() {
    # Imprime el comentario del encabezado (hasta la primera línea que no es comentario).
    awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "$0"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--namespace) NAMESPACE="$2"; shift 2 ;;
        --unit) RUN_UNIT=1; shift ;;
        --failover) RUN_FAILOVER=1; shift ;;
        --keep) KEEP_POD=1; shift ;;
        -h|--help) usage ;;
        *) echo "Opción desconocida: $1 (use -h)"; exit 2 ;;
    esac
done

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ---------------------------------------------------------------- salida
if [ -t 1 ]; then
    G=$'\e[32m'; R=$'\e[31m'; Y=$'\e[33m'; B=$'\e[1m'; N=$'\e[0m'
else
    G=""; R=""; Y=""; B=""; N=""
fi
PASS=0
FAIL=0
FAILED=()

section() { printf "\n${B}== %s${N}\n" "$1"; }
pass()    { PASS=$((PASS + 1)); printf "  ${G}✔${N} %s\n" "$1"; }
fail()    { FAIL=$((FAIL + 1)); FAILED+=("$1"); printf "  ${R}✘${N} %s\n      ${R}%s${N}\n" "$1" "$2"; }
info()    { printf "  ${Y}ℹ${N} %s\n" "$1"; }
die()     { printf "${R}%s${N}\n" "$1" >&2; exit 1; }

kc()    { kubectl -n "$NAMESPACE" "$@"; }
kexec() { kc exec "$POD" -- "$@"; }

cleanup() {
    if [ "$KEEP_POD" -eq 0 ]; then
        kc delete pod "$POD" --ignore-not-found --wait=false >/dev/null 2>&1
    else
        info "Pod $POD conservado: kubectl -n $NAMESPACE exec -it $POD -- sh"
    fi
}

# ---------------------------------------------------------------- HTTP
# req <args de curl>: ejecuta curl en el pod y deja STATUS, HEADERS y BODY.
STATUS=""; HEADERS=""; BODY=""
req() {
    local raw
    raw="$(kexec curl -sik -m 20 "$@" 2>/dev/null)"
    HEADERS="$(printf '%s\n' "$raw" | sed '/^\r\{0,1\}$/q')"
    BODY="$(printf '%s\n' "$raw" | sed '1,/^\r\{0,1\}$/d')"
    STATUS="$(printf '%s\n' "$HEADERS" | head -1 | awk '{print $2}')"
    [ -n "$STATUS" ] || STATUS="sin-respuesta"
}

# hdr <nombre>: valor del encabezado en la última respuesta (el último si se repite).
hdr() {
    printf '%s\n' "$HEADERS" | grep -i "^$1:" | tail -1 | cut -d: -f2- | tr -d '\r' | sed 's/^ *//'
}

# expect <descripción> <status> [x-cache] [zona]
expect() {
    local desc="$1" want_status="$2" want_cache="${3:-}" want_zone="${4:-}"
    local got_cache got_zone problems=""
    got_cache="$(hdr X-Cache)"
    got_zone="$(hdr X-Zonal-Cache)"
    [ "$STATUS" = "$want_status" ] || problems+="status=$STATUS (esperado $want_status) "
    if [ -n "$want_cache" ] && [ "$got_cache" != "$want_cache" ]; then
        problems+="X-Cache=${got_cache:-<vacío>} (esperado $want_cache) "
    fi
    if [ -n "$want_zone" ] && [ "$got_zone" != "$want_zone" ]; then
        problems+="X-Zonal-Cache=${got_zone:-<vacío>} (esperado $want_zone)"
    fi
    if [ -z "$problems" ]; then
        local extra="$STATUS"
        [ -n "$got_cache" ] && extra+=", X-Cache: $got_cache"
        [ -n "$got_zone" ] && extra+=", zona: $got_zone"
        pass "$desc ($extra)"
    else
        fail "$desc" "$problems"
    fi
}

svc_ip() { kc get svc "$1" -o jsonpath='{.spec.clusterIP}' 2>/dev/null; }

# ================================================================ 0. requisitos
section "0. Requisitos"
command -v kubectl >/dev/null || die "kubectl no está instalado."
kubectl cluster-info >/dev/null 2>&1 || die "No hay conexión con el cluster (¿Kubernetes activado en Docker Desktop?)."
pass "Cluster accesible (contexto: $(kubectl config current-context))"

APACHE_SVC="$APACHE_RELEASE-apache"
REST_SVC="$REST_RELEASE-rest-api-java"
DNS_SVC="$DNS_RELEASE-dns-interceptor"
declare -A ZONE_IP

missing=0
for d in "$APACHE_SVC" "$REST_SVC" "$DNS_SVC" $(for z in $ZONES; do echo "$CACHE_RELEASE-zonal-cache-$z"; done); do
    if kc rollout status deploy/"$d" --timeout=90s >/dev/null 2>&1; then
        pass "Deployment $d disponible"
    else
        fail "Deployment $d disponible" "no existe o no está listo (kubectl -n $NAMESPACE get pods)"
        missing=1
    fi
done
[ "$missing" -eq 0 ] || die "Faltan componentes. Instale los charts (ver encabezado de este script)."

for z in $ZONES; do
    ZONE_IP[$z]="$(svc_ip "$CACHE_RELEASE-zonal-cache-$z")"
    info "Caché $z -> ${ZONE_IP[$z]}"
done
DNS_IP="$(svc_ip "$DNS_SVC")"
DNS_PORT="$(kc get svc "$DNS_SVC" -o jsonpath='{.spec.ports[0].port}')"
DNS_NODEPORT="$(kc get svc "$DNS_SVC" -o jsonpath='{.spec.ports[0].nodePort}' 2>/dev/null)"
info "DNS Interceptor -> $DNS_IP:$DNS_PORT ($(kc get svc "$DNS_SVC" -o jsonpath='{.spec.ports[*].protocol}' | tr ' ' '+'), NodePort ${DNS_NODEPORT:-ninguno})"

# Pod de pruebas
trap cleanup EXIT
kc delete pod "$POD" --ignore-not-found --wait=true >/dev/null 2>&1
kc run "$POD" --image="$TESTER_IMAGE" --restart=Never --command -- sleep 3600 >/dev/null \
    || die "No se pudo crear el pod $POD."
kc wait --for=condition=Ready pod/"$POD" --timeout=120s >/dev/null || die "El pod $POD no quedó listo."
kexec apk add --no-cache curl bind-tools openssl >/dev/null 2>&1 || die "No se pudieron instalar curl y bind-tools en el pod (¿sin Internet?)."
pass "Pod de pruebas $POD listo (curl, dig, nslookup, openssl)"

RUN="$(date +%s)-$$"   # hace únicas las URLs para que la primera petición sea MISS

# ================================================================ 1. orígenes
section "1. Servidores de origen (directo, sin caché)"
req "http://$APACHE_SVC/"
expect "Apache sirve el HTML" 200
req "http://$REST_SVC:8080/health"
expect "Rest API Java /health" 200

# ================================================================ 2. DNS
section "2. DNS Interceptor (geolocalización por EDNS Client Subnet)"
dns_query() { kexec dig +short +tries=3 +time=2 @"$DNS_IP" -p "$DNS_PORT" "$@" 2>/dev/null | grep -E '^[0-9.]+$' | head -1; }

for subnet in "${!SUBNET_ZONE[@]}"; do
    zone="${SUBNET_ZONE[$subnet]}"
    [ -n "${ZONE_IP[$zone]:-}" ] || continue
    got="$(dns_query "$APACHE_HOST" +subnet="$subnet")"
    if [ "$got" = "${ZONE_IP[$zone]}" ]; then
        pass "$APACHE_HOST desde $subnet -> $got (caché $zone)"
    else
        fail "$APACHE_HOST desde $subnet" "respondió '${got:-nada}', esperado ${ZONE_IP[$zone]} ($zone)"
    fi
done

got="$(dns_query "$API_HOST" +subnet=80.58.61.0/24)"
[ "$got" = "${ZONE_IP[europa]:-x}" ] \
    && pass "Comodín *.cdn.p2.local: $API_HOST -> $got (europa)" \
    || fail "Comodín *.cdn.p2.local" "respondió '${got:-nada}', esperado ${ZONE_IP[europa]:-?}"

got="$(kexec nslookup "$APACHE_HOST" "$DNS_IP" 2>/dev/null | awk '/^Address: /{print $2}' | tail -1)"
if [ -n "$got" ] && [[ " ${ZONE_IP[*]} " == *" $got "* ]]; then
    pass "nslookup sin subred (IP privada) -> $got (zona por defecto)"
else
    fail "nslookup $APACHE_HOST" "respondió '${got:-nada}'"
fi

got="$(dns_query google.com)"
if [ -n "$got" ] && [[ " ${ZONE_IP[*]} " != *" $got "* ]]; then
    pass "Dominio no interceptado (google.com) se reenvía al DNS externo -> $got"
else
    fail "Reenvío de google.com" "respondió '${got:-nada}'"
fi

# DNS sobre TCP (RFC 7766): solo si el Service publica también el puerto TCP.
if kc get svc "$DNS_SVC" -o jsonpath='{.spec.ports[*].protocol}' | grep -qw TCP; then
    got="$(dns_query +tcp "$APACHE_HOST" +subnet=1.0.16.0/24)"
    [ "$got" = "${ZONE_IP[asia]:-x}" ] \
        && pass "DNS sobre TCP: $APACHE_HOST desde 1.0.16.0/24 -> $got (asia)" \
        || fail "DNS sobre TCP" "respondió '${got:-nada}', esperado ${ZONE_IP[asia]:-?}"
else
    info "El Service del DNS Interceptor no publica TCP; se omite la prueba de DNS sobre TCP."
fi

if [ -n "$DNS_NODEPORT" ] && command -v dig >/dev/null; then
    got="$(dig +short +tries=2 +time=2 @127.0.0.1 -p "$DNS_NODEPORT" "$APACHE_HOST" +subnet=80.58.61.0/24 2>/dev/null | head -1)"
    if [ "$got" = "${ZONE_IP[europa]:-x}" ]; then
        pass "Desde el equipo: dig @127.0.0.1 -p $DNS_NODEPORT -> $got (europa)"
    else
        info "Desde el equipo, el NodePort $DNS_NODEPORT/udp no respondió como se esperaba ('${got:-nada}'); las pruebas dentro del cluster sí cuentan."
    fi
fi

# ================================================================ 3. caché por zona
section "3. Zonal Cache por zona (health, MISS -> HIT, independencia)"
for z in $ZONES; do
    ip="${ZONE_IP[$z]}"
    req "http://$ip/__zc/health"
    expect "[$z] /__zc/health" 200 "" "$z"

    # Misma URL en todas las zonas: cada caché es independiente, así que la
    # primera petición en cada zona es MISS aunque otra zona ya la tenga.
    url="http://$APACHE_HOST/?run=$RUN"
    req --resolve "$APACHE_HOST:80:$ip" "$url"
    expect "[$z] Apache 1.ª petición" 200 MISS "$z"
    req --resolve "$APACHE_HOST:80:$ip" "$url"
    expect "[$z] Apache 2.ª petición" 200 HIT "$z"
    age="$(hdr Age)"
    [ -n "$age" ] && pass "[$z] El HIT incluye Age: $age s" || fail "[$z] Encabezado Age en el HIT" "no vino"
done

# ================================================================ 4. HTTPS
section "4. SSL termination (HTTPS en la caché, HTTP hacia el origen)"
ip="${ZONE_IP[europa]:-${ZONE_IP[$(echo $ZONES | awk '{print $1}')]}}"
req --resolve "$APACHE_HOST:443:$ip" "https://$APACHE_HOST/?tls=$RUN"
expect "HTTPS 1.ª petición" 200 MISS
req --resolve "$APACHE_HOST:443:$ip" "https://$APACHE_HOST/?tls=$RUN"
expect "HTTPS 2.ª petición" 200 HIT
subject="$(kexec sh -c "echo | timeout 5 openssl s_client -connect $ip:443 -servername $APACHE_HOST 2>/dev/null | grep -m1 -E '^subject='" 2>/dev/null)"
[ -n "$subject" ] && info "Certificado presentado: $subject" || info "No se pudo leer el certificado con openssl (no afecta el resultado)."

# ================================================================ 5. Rest API Java
section "5. Rest API Java a través de la caché (x-api-key, GET/POST/PUT/DELETE)"
RES=(--resolve "$API_HOST:80:$ip")
K=(-H "x-api-key: $API_KEY")
JSON=(-H "Content-Type: application/json")

req "${RES[@]}" "http://$API_HOST/api/hello?run=$RUN"
expect "/api/hello sin autenticación, 1.ª (FIFO)" 200 MISS
req "${RES[@]}" "http://$API_HOST/api/hello?run=$RUN"
expect "/api/hello sin autenticación, 2.ª" 200 HIT

req "${RES[@]}" "http://$API_HOST/api/items"
expect "/api/items sin x-api-key" 401
req "${RES[@]}" -H "x-api-key: clave-incorrecta" "http://$API_HOST/api/items"
expect "/api/items con clave incorrecta" 401

req "${RES[@]}" "${K[@]}" "http://$API_HOST/api/items?run=$RUN"
expect "GET /api/items con clave válida, 1.ª (LFU)" 200 MISS
req "${RES[@]}" "${K[@]}" "http://$API_HOST/api/items?run=$RUN"
expect "GET /api/items con clave válida, 2.ª" 200 HIT

NAME="item-$RUN"
req "${RES[@]}" "${K[@]}" "${JSON[@]}" -X POST -d "{\"name\":\"$NAME\",\"description\":\"prueba\"}" "http://$API_HOST/api/items"
expect "POST /api/items (no se cachea)" 201 BYPASS
ID="$(printf '%s' "$BODY" | sed -n 's/.*"id":\([0-9]*\).*/\1/p' | head -1)"

if [ -n "$ID" ]; then
    info "Item creado con id $ID"
    req "${RES[@]}" "${K[@]}" "http://$API_HOST/api/items/$ID?run=$RUN"
    expect "GET /api/items/$ID" 200 MISS
    [[ "$BODY" == *"$NAME"* ]] && pass "El item devuelto tiene el nombre $NAME" || fail "Contenido del item" "$BODY"

    req "${RES[@]}" "${K[@]}" "http://$API_HOST/api/items?run=$RUN"
    if [ "$(hdr X-Cache)" = "HIT" ] && [[ "$BODY" != *"$NAME"* ]]; then
        info "Limitación esperada: la lista cacheada (HIT) todavía no incluye el item nuevo hasta que venza su TTL (10 s)."
    fi

    req "${RES[@]}" "${K[@]}" "${JSON[@]}" -X PUT -d '{"name":"actualizado","description":"put"}' "http://$API_HOST/api/items/$ID"
    expect "PUT /api/items/$ID" 200 BYPASS
    req "${RES[@]}" "${K[@]}" -X DELETE "http://$API_HOST/api/items/$ID"
    expect "DELETE /api/items/$ID" 204 BYPASS
    req "${RES[@]}" "${K[@]}" "http://$API_HOST/api/items/$ID?run=$RUN-borrado"
    expect "GET del item borrado (404, no se guarda en caché)" 404
else
    fail "Obtener el id del item creado" "respuesta del POST: $BODY"
fi

# ================================================================ 6. errores
section "6. Respuestas propias de la caché"
req -H "Host: no-administrado.com" "http://$ip/"
expect "Dominio no administrado" 404
req --resolve "$APACHE_HOST:80:$ip" -H "X-Zonal-Cache-Hop: 1" "http://$APACHE_HOST/"
expect "Bucle detectado (petición que ya pasó por una caché)" 508

# ================================================================ 7. sitios externos
section "7. Sitios externos (origen example.com por HTTP y por HTTPS)"
EXT_HTTP="externo-http.cdn.p2.local"
EXT_HTTPS="externo-https.cdn.p2.local"

req --resolve "$EXT_HTTP:80:$ip" "http://$EXT_HTTP/?run=$RUN"
expect "Sitio externo HTTP, 1.ª petición" 200 MISS
[[ "$BODY" == *"Example Domain"* ]] && pass "El contenido viene de http://example.com" \
    || fail "Contenido del sitio externo HTTP" "no contiene 'Example Domain' (¿el cluster tiene Internet?)"
req --resolve "$EXT_HTTP:80:$ip" "http://$EXT_HTTP/?run=$RUN"
expect "Sitio externo HTTP, 2.ª petición" 200 HIT

req --resolve "$EXT_HTTPS:80:$ip" "http://$EXT_HTTPS/?run=$RUN"
expect "Sitio externo HTTPS (caché -> https://example.com), 1.ª" 200 MISS
[[ "$BODY" == *"Example Domain"* ]] && pass "El contenido viene de https://example.com (TLS verificado hacia el origen)" \
    || fail "Contenido del sitio externo HTTPS" "no contiene 'Example Domain'"
req --resolve "$EXT_HTTPS:443:$ip" "https://$EXT_HTTPS/?run=$RUN"
expect "Sitio externo HTTPS de punta a punta (cliente -> HTTPS -> caché -> HTTPS), 2.ª" 200 HIT

# ================================================================ 8. TTL
section "8. TTL y qué se guarda en disco"
TTL_HOST="ttl.cdn.p2.local"
req --resolve "$TTL_HOST:80:$ip" "http://$TTL_HOST/api/hello?run=$RUN"
expect "TTL de 5 s, 1.ª petición" 200 MISS
req --resolve "$TTL_HOST:80:$ip" "http://$TTL_HOST/api/hello?run=$RUN"
expect "TTL de 5 s, antes de vencer" 200 HIT
info "Esperando 6 s a que venza el TTL..."
sleep 6
req --resolve "$TTL_HOST:80:$ip" "http://$TTL_HOST/api/hello?run=$RUN"
expect "TTL de 5 s, después de vencer (se vuelve a pedir al origen)" 200 MISS

req --resolve "$APACHE_HOST:80:$ip" "http://$APACHE_HOST/archivo.txt?run=$RUN"
expect "Extensión sin TTL en la regla (.txt) no se guarda" 404 BYPASS
req --resolve "$APACHE_HOST:80:$ip" "http://$APACHE_HOST/no-existe.html?run=$RUN"
expect "Respuesta 404 del origen, 1.ª" 404 MISS
req --resolve "$APACHE_HOST:80:$ip" "http://$APACHE_HOST/no-existe.html?run=$RUN"
expect "Respuesta 404 del origen, 2.ª (los errores no se guardan)" 404 MISS

# ================================================================ 9. reemplazo
section "9. Algoritmos de reemplazo (3 objetos caben; el 4.º expulsa uno)"
# Todas las URLs de un dominio de prueba piden /api/hello con distinto ?k=, así que todos
# los objetos pesan lo mismo (107 B). La partición admite 3. Los HIT no expulsan nada, por
# eso se comprueban primero los que deben seguir y al final la víctima (MISS).

# hget <host> <k>: GET /api/hello?k=<k> a la caché de la zona elegida.
hget() { req --resolve "$1:80:$ip" "http://$1/api/hello?run=$RUN&k=$2"; }
xc()   { hdr X-Cache; }

# check_policy <política> <host> "<accesos>" <víctima esperada> <explicación>
check_policy() {
    local name="$1" host="$2" accesses="$3" victim="$4" why="$5" k trace="" bad=""
    for k in 1 2 3; do hget "$host" "$k"; trace+="+$k($(xc)) "; [ "$(xc)" = MISS ] || bad+="inserción $k=$(xc) "; done
    for k in $accesses; do hget "$host" "$k"; trace+="lee$k($(xc)) "; [ "$(xc)" = HIT ] || bad+="acceso $k=$(xc) "; done
    hget "$host" 4; trace+="+4($(xc)) "; [ "$(xc)" = MISS ] || bad+="inserción 4=$(xc) "
    for k in 1 2 3 4; do
        [ "$k" = "$victim" ] && continue
        hget "$host" "$k"; trace+="¿$k?($(xc)) "; [ "$(xc)" = HIT ] || bad+="$k debía seguir (vino $(xc)) "
    done
    hget "$host" "$victim"; trace+="¿$victim?($(xc))"
    [ "$(xc)" = MISS ] || bad+="$victim debía haber salido (vino $(xc))"
    if [ -z "$bad" ]; then
        pass "$name: sale el $victim — $why"
        info "    $trace"
    else
        fail "$name: debía salir el $victim" "$bad | $trace"
    fi
}

check_policy LRU  lru.cdn.p2.local  "1"         2 "el usado hace más tiempo (el 1 se leyó después)"
check_policy LFU  lfu.cdn.p2.local  "1 1 2 2 3" 3 "el menos usado (2 usos; el 1 y el 2 tienen 3), aunque fue el último leído"
check_policy FIFO fifo.cdn.p2.local "1 1"       1 "el primero que entró, aunque es el más leído"
check_policy MRU  mru.cdn.p2.local  "2"         2 "el usado más recientemente"

# Random: la víctima no se puede predecir; se comprueba que salga exactamente uno de 1-3.
R_HOST="random.cdn.p2.local"; trace=""; bad=""
for k in 1 2 3; do hget "$R_HOST" "$k"; trace+="+$k($(xc)) "; done
hget "$R_HOST" 4; trace+="+4($(xc)) "
hget "$R_HOST" 4; trace+="¿4?($(xc)) "; [ "$(xc)" = HIT ] || bad+="el 4 recién guardado debía estar "
victim=""
for k in 1 2 3; do
    hget "$R_HOST" "$k"; trace+="¿$k?($(xc)) "
    if [ "$(xc)" = MISS ]; then victim="$k"; break; fi   # este MISS expulsa otro: se para aquí
done
if [ -z "$bad" ] && [ -n "$victim" ]; then
    pass "Random: salió el $victim (al azar; puede cambiar en cada ejecución)"
    info "    $trace"
else
    fail "Random: debía salir uno de los objetos 1-3" "${bad}ninguno faltaba | $trace"
fi

# ================================================================ 10. límite por dominio
section "10. Límite de tamaño por dominio"
# limite.cdn.p2.local: dominio 440 B; /api/hello y /health tienen 350 B cada una.
# 3 × /api/hello (321 B) + 1 × /health (80 B) = 401 B caben. El 2.º /health deja el dominio
# en 481 B > 440 B: se expulsa un /health aunque su propia URL (160 B de 350 B) tenía espacio.
L_HOST="limite.cdn.p2.local"; trace=""; bad=""
lget() { req --resolve "$L_HOST:80:$ip" "http://$L_HOST$1?run=$RUN&k=$2"; }
for k in 1 2 3; do lget /api/hello "$k"; trace+="hello$k($(xc)) "; done
lget /health 1; trace+="health1($(xc)) "
lget /health 2; trace+="health2($(xc)) "
for k in 1 2 3; do lget /api/hello "$k"; trace+="¿hello$k?($(xc)) "; [ "$(xc)" = HIT ] || bad+="hello$k debía seguir "; done
lget /health 2; trace+="¿health2?($(xc)) "; [ "$(xc)" = HIT ] || bad+="health2 debía seguir "
lget /health 1; trace+="¿health1?($(xc))"; [ "$(xc)" = MISS ] || bad+="health1 debía haber salido por el límite del dominio"
if [ -z "$bad" ]; then
    pass "Al pasar los 440 B del dominio se expulsó health1, aunque /health tenía espacio propio"
    info "    $trace"
else
    fail "Límite de tamaño por dominio" "$bad | $trace"
fi

# ================================================================ 11. usuario y contraseña
section "11. Usuario y contraseña (sesión JWT)"
P_HOST="privado.cdn.p2.local"
PRES=(--resolve "$P_HOST:80:$ip")

# jwt <dominio> <regla> <exp> <secreto>: JWT HS256 igual al que emite el API Node.js.
jwt() {
    kexec sh -c 'b() { openssl base64 -A | tr "+/" "-_" | tr -d "="; }
        h=$(printf "{\"alg\":\"HS256\",\"typ\":\"JWT\"}" | b)
        p=$(printf "{\"domain\":\"%s\",\"sub\":\"ana\",\"rule\":\"%s\",\"exp\":%s}" "$1" "$2" "$3" | b)
        s=$(printf "%s.%s" "$h" "$p" | openssl dgst -sha256 -hmac "$4" -binary | b)
        printf "%s.%s.%s" "$h" "$p" "$s"' sh "$1" "$2" "$3" "$4"
}
NOW="$(date +%s)"
TOKEN="$(jwt "$P_HOST" privado $((NOW + 3600)) "$SESSION_SECRET")"
OTHER_RULE="$(jwt "$P_HOST" privado-otra $((NOW + 3600)) "$SESSION_SECRET")"
EXPIRED="$(jwt "$P_HOST" privado $((NOW - 60)) "$SESSION_SECRET")"
BAD_SIG="$(jwt "$P_HOST" privado $((NOW + 3600)) "otro-secreto")"
OTHER_DOMAIN="$(jwt "$APACHE_HOST" privado $((NOW + 3600)) "$SESSION_SECRET")"
[ -n "$TOKEN" ] || fail "Generar el JWT en el pod" "openssl no devolvió nada"

req "${PRES[@]}" "http://$P_HOST/?run=$RUN"
loc="$(hdr Location)"
expect "Sin sesión: redirige al formulario de login" 302
[[ "$loc" == "$LOGIN_URL?domain=$P_HOST&path=%2F&callback="* ]] \
    && pass "Location incluye domain, path y callback: ${loc:0:110}..." \
    || fail "Location de la redirección al login" "${loc:-<vacío>}"
req "${PRES[@]}" -X POST -d 'x=1' "http://$P_HOST/"
expect "Sin sesión y no es GET: 401 (sin redirección)" 401

req "${PRES[@]}" "http://$P_HOST/__zc/auth?next=%2F%3Frun%3D$RUN&token=$TOKEN"
expect "Callback /__zc/auth con token válido" 302
cookie="$(hdr Set-Cookie)"
[[ "$cookie" == zc_session=* && "$cookie" == *HttpOnly* ]] \
    && pass "Set-Cookie: zc_session (HttpOnly) y Location: $(hdr Location)" \
    || fail "Cookie de sesión" "${cookie:-<sin Set-Cookie>}"
req "${PRES[@]}" "http://$P_HOST/__zc/auth?next=%2F%2Fevil.com&token=$TOKEN"
[ "$(hdr Location)" = "/" ] && pass "next externo (//evil.com) se ignora: redirige a /" \
    || fail "Redirección abierta en /__zc/auth" "Location: $(hdr Location)"
req "${PRES[@]}" "http://$P_HOST/__zc/auth?token=$BAD_SIG"
expect "Callback con token de firma inválida" 401

req "${PRES[@]}" -H "Cookie: zc_session=$TOKEN" "http://$P_HOST/?run=$RUN"
expect "Con la cookie de sesión, 1.ª" 200 MISS
req "${PRES[@]}" -H "Cookie: zc_session=$TOKEN" "http://$P_HOST/?run=$RUN"
expect "Con la cookie de sesión, 2.ª" 200 HIT
req "${PRES[@]}" -H "Authorization: Bearer $TOKEN" "http://$P_HOST/?run=$RUN"
expect "Con Authorization: Bearer (clientes que no son navegador)" 200 HIT

req "${PRES[@]}" -H "Cookie: zc_session=$OTHER_RULE" "http://$P_HOST/?run=$RUN"
expect "Sesión de otra regla del mismo dominio (cookie): se pide login" 302
req "${PRES[@]}" -H "Authorization: Bearer $OTHER_RULE" "http://$P_HOST/?run=$RUN"
expect "Sesión de otra regla del mismo dominio (Bearer)" 401
req "${PRES[@]}" -H "Cookie: zc_session=$EXPIRED" "http://$P_HOST/?run=$RUN"
expect "Sesión vencida (cookie): se pide login de nuevo" 302
req "${PRES[@]}" -H "Authorization: Bearer $EXPIRED" "http://$P_HOST/?run=$RUN"
expect "Sesión vencida (Bearer)" 401
req "${PRES[@]}" -H "Authorization: Bearer $BAD_SIG" "http://$P_HOST/?run=$RUN"
expect "Token firmado con otro secreto" 401
req "${PRES[@]}" -H "Authorization: Bearer $OTHER_DOMAIN" "http://$P_HOST/?run=$RUN"
expect "Token de otro dominio" 401

req "${PRES[@]}" "http://$P_HOST/__zc/logout?next=%2F"
cookie="$(hdr Set-Cookie)"
[[ "$STATUS" = 302 && "$cookie" == *"Max-Age=0"* ]] \
    && pass "/__zc/logout borra la cookie (Max-Age=0) y redirige a /" \
    || fail "Logout" "status=$STATUS Set-Cookie=${cookie:-<vacío>}"

# ================================================================ 12. unitarias
if [ "$RUN_UNIT" -eq 1 ]; then
    section "12. Pruebas unitarias (algoritmos de reemplazo, límites de tamaño, auth, DNS)"
    out="$(docker build --progress=plain --no-cache-filter test --target test "$REPO_ROOT/zonal-cache" 2>&1 | grep -E 'pruebas, [0-9]+ fallidas' | tail -1 | sed -E 's/^#[0-9]+ [0-9.]+ //')"
    [[ "$out" == *" 0 fallidas"* ]] && pass "Zonal Cache: $out" || fail "Pruebas unitarias de la Zonal Cache" "${out:-no se pudo ejecutar docker build --target test}"
    if command -v make >/dev/null && command -v gcc >/dev/null; then
        out="$(make -s -C "$REPO_ROOT/dns-interceptor" test 2>&1 | grep -E 'verificaciones' | tail -1)"
        [[ "$out" == *" 0 fallidas"* ]] && pass "DNS Interceptor: $out" || fail "Pruebas unitarias del DNS Interceptor" "${out:-falló make test}"
    else
        info "Sin gcc/make: se omiten las pruebas unitarias del DNS Interceptor."
    fi
else
    info "Pruebas unitarias omitidas (use --unit para incluirlas)."
fi

# ================================================================ 8. respaldo de zonas
# El DNS Interceptor sondea cada caché por TCP (DI_HEALTH_*). Si la zona del país no
# responde, contesta con la zona por defecto o con otra zona sana.
if [ "$RUN_FAILOVER" -eq 1 ]; then
    section "13. Respaldo de zonas (caché europa apagada)"
    EU_DEPLOY="$CACHE_RELEASE-zonal-cache-europa"
    restore_europa() {
        kc scale deploy/"$EU_DEPLOY" --replicas=1 >/dev/null 2>&1
        kc rollout status deploy/"$EU_DEPLOY" --timeout=120s >/dev/null 2>&1
    }
    if [ -z "${ZONE_IP[europa]:-}" ] || ! kc get deploy "$EU_DEPLOY" >/dev/null 2>&1; then
        info "No existe la zona europa; se omite la prueba."
    else
        kc scale deploy/"$EU_DEPLOY" --replicas=0 >/dev/null
        kc wait --for=delete pod -l "app.kubernetes.io/instance=$CACHE_RELEASE,zonal-cache/zone=europa" --timeout=60s >/dev/null 2>&1
        got=""
        for _ in $(seq 1 15); do   # el sondeo corre cada 5 s por defecto
            got="$(dns_query "$APACHE_HOST" +subnet=80.58.61.0/24)"
            [ -n "$got" ] && [ "$got" != "${ZONE_IP[europa]}" ] && break
            sleep 2
        done
        if [ -n "$got" ] && [ "$got" != "${ZONE_IP[europa]}" ] && [[ " ${ZONE_IP[*]} " == *" $got "* ]]; then
            pass "Cliente de España con europa apagada -> $got (otra zona sana)"
        else
            fail "Respaldo de zonas" "con europa apagada respondió '${got:-nada}'"
        fi
        restore_europa
        got=""
        for _ in $(seq 1 15); do
            got="$(dns_query "$APACHE_HOST" +subnet=80.58.61.0/24)"
            [ "$got" = "${ZONE_IP[europa]}" ] && break
            sleep 2
        done
        [ "$got" = "${ZONE_IP[europa]}" ] \
            && pass "Europa encendida de nuevo: el cliente de España vuelve a $got (europa)" \
            || fail "Recuperación de la zona europa" "respondió '${got:-nada}'"
    fi
fi

# ================================================================ resumen
section "Resumen"
printf "  ${G}%d correctas${N}, ${R}%d fallidas${N}\n" "$PASS" "$FAIL"
for f in "${FAILED[@]}"; do printf "    ${R}- %s${N}\n" "$f"; done
[ "$FAIL" -eq 0 ]
