#include "domains.h"

#include <string.h>

#define JSON_MAX_DEPTH 32

typedef struct {
    const char *p;
} json_t;

static void skip_ws(json_t *j) {
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++;
}

/* Lee una cadena JSON. Si out es NULL solo la salta. Devuelve su largo, o -1 si es inválida.
 * Las secuencias \uXXXX se reemplazan por '?': un nombre de dominio válido no las usa. */
static int parse_string(json_t *j, char *out, size_t cap) {
    if (*j->p != '"') return -1;
    j->p++;
    size_t n = 0;
    while (*j->p && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            char e = *j->p++;
            switch (e) {
            case '"': case '\\': case '/': c = e; break;
            case 'n': case 't': case 'r': case 'b': case 'f': c = '?'; break;
            case 'u':
                for (int i = 0; i < 4; i++) {
                    if (!*j->p) return -1;
                    j->p++;
                }
                c = '?';
                break;
            default: return -1;
            }
        }
        if (out && n + 1 < cap) out[n] = c;
        n++;
    }
    if (*j->p != '"') return -1;
    j->p++;
    if (out) out[n < cap ? n : cap - 1] = '\0';
    return (int)n;
}

static int skip_value(json_t *j, int depth);

static int skip_container(json_t *j, char open, char close, int depth) {
    j->p++; /* abre */
    skip_ws(j);
    if (*j->p == close) {
        j->p++;
        return 0;
    }
    for (;;) {
        skip_ws(j);
        if (open == '{') {
            if (parse_string(j, NULL, 0) < 0) return -1;
            skip_ws(j);
            if (*j->p++ != ':') return -1;
        }
        if (skip_value(j, depth + 1) != 0) return -1;
        skip_ws(j);
        if (*j->p == ',') {
            j->p++;
            continue;
        }
        if (*j->p == close) {
            j->p++;
            return 0;
        }
        return -1;
    }
}

static int skip_value(json_t *j, int depth) {
    if (depth > JSON_MAX_DEPTH) return -1;
    skip_ws(j);
    switch (*j->p) {
    case '"': return parse_string(j, NULL, 0) < 0 ? -1 : 0;
    case '{': return skip_container(j, '{', '}', depth);
    case '[': return skip_container(j, '[', ']', depth);
    case '\0': return -1;
    default: { /* número, true, false o null */
        const char *start = j->p;
        while (*j->p && !strchr(",]} \t\r\n", *j->p)) j->p++;
        return j->p == start ? -1 : 0;
    }
    }
}

/* Normaliza y valida un nombre de dominio. Devuelve 1 si sirve y 0 si hay que descartarlo. */
static int normalize_name(char *name) {
    size_t len = strlen(name);
    while (len > 0 && name[len - 1] == '.') name[--len] = '\0';
    if (len == 0 || len >= CFG_NAME_LEN) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') name[i] = c = (char)(c + 32);
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
        if (!ok) return 0;
    }
    return name[0] != '.' && !strstr(name, "..");
}

static void list_add(domain_list_t *list, char *name) {
    if (!normalize_name(name) || list->count == DYN_MAX_DOMAINS) return;
    for (size_t i = 0; i < list->count; i++)
        if (strcmp(list->names[i], name) == 0) return;
    strcpy(list->names[list->count++], name);
}

/* Recorre un objeto de dominio y toma su "name". */
static int read_domain_object(json_t *j, domain_list_t *out) {
    j->p++; /* '{' */
    skip_ws(j);
    if (*j->p == '}') {
        j->p++;
        return 0;
    }
    for (;;) {
        char key[32];
        skip_ws(j);
        if (parse_string(j, key, sizeof(key)) < 0) return -1;
        skip_ws(j);
        if (*j->p++ != ':') return -1;
        skip_ws(j);
        if (strcmp(key, "name") == 0 && *j->p == '"') {
            char name[CFG_NAME_LEN + 1];
            int n = parse_string(j, name, sizeof(name));
            if (n < 0) return -1;
            if ((size_t)n < sizeof(name)) list_add(out, name);
        } else if (skip_value(j, 3) != 0) {
            return -1;
        }
        skip_ws(j);
        if (*j->p == ',') {
            j->p++;
            continue;
        }
        if (*j->p == '}') {
            j->p++;
            return 0;
        }
        return -1;
    }
}

static int read_domains_array(json_t *j, domain_list_t *out) {
    j->p++; /* '[' */
    skip_ws(j);
    if (*j->p == ']') {
        j->p++;
        return 0;
    }
    for (;;) {
        skip_ws(j);
        if (*j->p == '{') {
            if (read_domain_object(j, out) != 0) return -1;
        } else if (skip_value(j, 2) != 0) {
            return -1;
        }
        skip_ws(j);
        if (*j->p == ',') {
            j->p++;
            continue;
        }
        if (*j->p == ']') {
            j->p++;
            return 0;
        }
        return -1;
    }
}

int domains_parse_json(const char *json, domain_list_t *out) {
    memset(out, 0, sizeof(*out));
    if (!json) return -1;
    json_t j = {json};
    int found = 0;

    skip_ws(&j);
    if (*j.p++ != '{') return -1;
    skip_ws(&j);
    if (*j.p == '}') return -1;
    for (;;) {
        char key[32];
        skip_ws(&j);
        if (parse_string(&j, key, sizeof(key)) < 0) return -1;
        skip_ws(&j);
        if (*j.p++ != ':') return -1;
        skip_ws(&j);
        if (strcmp(key, "domains") == 0 && *j.p == '[') {
            if (read_domains_array(&j, out) != 0) return -1;
            found = 1;
        } else if (skip_value(&j, 1) != 0) {
            return -1;
        }
        skip_ws(&j);
        if (*j.p == ',') {
            j.p++;
            continue;
        }
        if (*j.p == '}') break;
        return -1;
    }
    return found ? 0 : -1;
}

void dyn_domains_init(dyn_domains_t *d) {
    memset(d, 0, sizeof(*d));
    pthread_mutex_init(&d->lock, NULL);
}

int dyn_domains_replace(dyn_domains_t *d, const domain_list_t *list) {
    pthread_mutex_lock(&d->lock);
    int changed = d->list.count != list->count;
    for (size_t i = 0; !changed && i < list->count; i++)
        changed = strcmp(d->list.names[i], list->names[i]) != 0;
    if (changed) d->list = *list;
    pthread_mutex_unlock(&d->lock);
    return changed;
}

size_t dyn_domains_count(dyn_domains_t *d) {
    pthread_mutex_lock(&d->lock);
    size_t n = d->list.count;
    pthread_mutex_unlock(&d->lock);
    return n;
}

int dyn_domains_match(dyn_domains_t *d, const char *name) {
    size_t nlen = strlen(name);
    int hit = 0;
    pthread_mutex_lock(&d->lock);
    for (size_t i = 0; i < d->list.count && !hit; i++) {
        const char *dom = d->list.names[i];
        size_t dlen = strlen(dom);
        if (nlen == dlen && strcmp(name, dom) == 0) hit = 1;
        else if (nlen > dlen && name[nlen - dlen - 1] == '.' && strcmp(name + nlen - dlen, dom) == 0)
            hit = 1;
    }
    pthread_mutex_unlock(&d->lock);
    return hit;
}
