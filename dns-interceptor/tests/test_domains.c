#include <string.h>

#include "../src/domains.h"
#include "test.h"

static void test_parse_ok(void) {
    domain_list_t l;
    const char *json =
        "{\"domains\":[{\"name\":\"Ejemplo.com\",\"originUrl\":\"http://x\",\"maxCacheBytes\":1048576,"
        "\"rules\":[{\"id\":\"r1\",\"name\":\"no-es-dominio\",\"apiKeys\":[\"a\",\"b\"]}]},"
        "{\"name\":\"otro.org.\"}],\"extra\":{\"x\":[1,2,{\"y\":null}]}}";

    CHECK(domains_parse_json(json, &l) == 0);
    CHECK(l.count == 2);
    CHECK_STR(l.names[0], "ejemplo.com"); /* minúsculas */
    CHECK_STR(l.names[1], "otro.org");    /* sin punto final */
}

static void test_parse_filters(void) {
    domain_list_t l;

    /* Repetidos, nombres inválidos y vacíos se descartan; "name" que no es cadena se ignora. */
    const char *json = "{\"domains\":[{\"name\":\"a.com\"},{\"name\":\"A.COM\"},{\"name\":\"\"},"
                       "{\"name\":\"mal nombre.com\"},{\"name\":\"..x\"},{\"name\":42},{},7,"
                       "{\"name\":\"b.com\"}]}";
    CHECK(domains_parse_json(json, &l) == 0);
    CHECK(l.count == 2);
    CHECK_STR(l.names[0], "a.com");
    CHECK_STR(l.names[1], "b.com");

    CHECK(domains_parse_json("{\"domains\":[]}", &l) == 0 && l.count == 0);
    CHECK(domains_parse_json("{ \"domains\" : [ { \"name\" : \"c.com\" } ] }", &l) == 0 && l.count == 1);
}

static void test_parse_invalid(void) {
    domain_list_t l;

    CHECK(domains_parse_json(NULL, &l) == -1);
    CHECK(domains_parse_json("", &l) == -1);
    CHECK(domains_parse_json("[]", &l) == -1);                       /* la raíz debe ser un objeto */
    CHECK(domains_parse_json("{}", &l) == -1);                       /* falta "domains" */
    CHECK(domains_parse_json("{\"domains\":{}}", &l) == -1);         /* "domains" debe ser arreglo */
    CHECK(domains_parse_json("{\"domains\":[{\"name\":\"a.com\"}", &l) == -1); /* truncado */
    CHECK(domains_parse_json("{\"domains\":[{\"name\":\"a.com\"},]}", &l) == -1);
    CHECK(domains_parse_json("<html>502 Bad Gateway</html>", &l) == -1);

    /* Anidamiento excesivo: no debe desbordar la pila. */
    char deep[512];
    memset(deep, '[', sizeof(deep) - 1);
    deep[sizeof(deep) - 1] = '\0';
    char json[600];
    snprintf(json, sizeof(json), "{\"x\":%s}", deep);
    CHECK(domains_parse_json(json, &l) == -1);
}

static void test_dynamic_set(void) {
    dyn_domains_t d;
    domain_list_t l = {0};

    dyn_domains_init(&d);
    CHECK(dyn_domains_count(&d) == 0);
    CHECK(dyn_domains_match(&d, "ejemplo.com") == 0);

    strcpy(l.names[0], "ejemplo.com");
    strcpy(l.names[1], "api.otro.org");
    l.count = 2;
    CHECK(dyn_domains_replace(&d, &l) == 1); /* cambió */
    CHECK(dyn_domains_replace(&d, &l) == 0); /* igual: no cambia */
    CHECK(dyn_domains_count(&d) == 2);

    CHECK(dyn_domains_match(&d, "ejemplo.com") == 1);
    CHECK(dyn_domains_match(&d, "www.ejemplo.com") == 1);   /* subdominio */
    CHECK(dyn_domains_match(&d, "a.b.ejemplo.com") == 1);
    CHECK(dyn_domains_match(&d, "xejemplo.com") == 0);      /* no es subdominio */
    CHECK(dyn_domains_match(&d, "ejemplo.com.mx") == 0);
    CHECK(dyn_domains_match(&d, "com") == 0);
    CHECK(dyn_domains_match(&d, "otro.org") == 0);          /* el padre no queda cubierto */
    CHECK(dyn_domains_match(&d, "api.otro.org") == 1);

    l.count = 0;
    CHECK(dyn_domains_replace(&d, &l) == 1); /* un dominio dado de baja deja de interceptarse */
    CHECK(dyn_domains_match(&d, "ejemplo.com") == 0);
}

void run_domains_tests(void) {
    test_parse_ok();
    test_parse_filters();
    test_parse_invalid();
    test_dynamic_set();
}
