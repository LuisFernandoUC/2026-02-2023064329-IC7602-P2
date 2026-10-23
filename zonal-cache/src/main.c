/*
 * main.c - Punto de entrada de la Zonal Cache.
 *
 * 1. Lee la configuración del proceso desde variables de entorno.
 * 2. Crea la caché en disco y descarga la configuración de dominios (API Node.js).
 * 3. Inicia un hilo que refresca esa configuración periódicamente.
 * 4. Atiende peticiones HTTP/HTTPS hasta recibir SIGINT o SIGTERM.
 */
#include "cache.h"
#include "config_store.h"
#include "log.h"
#include "origin.h"
#include "proxy.h"
#include "server.h"
#include "settings.h"

#include <curl/curl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

typedef struct {
    config_store_t *store;
    const settings_t *settings;
} refresher_args_t;

static void *refresh_loop(void *arg)
{
    refresher_args_t *a = arg;
    while (!stop_requested) {
        for (int i = 0; i < a->settings->config_refresh_seconds && !stop_requested; i++)
            sleep(1);
        if (stop_requested)
            break;
        char err[256];
        if (config_store_refresh(a->store, a->settings, err, sizeof(err)) != 0)
            LOG_W("no se pudo refrescar la configuración: %s", err);
        else
            LOG_D("configuración refrescada");
    }
    return NULL;
}

int main(void)
{
    settings_t settings;
    char err[256];
    if (settings_load(&settings, err, sizeof(err)) != 0) {
        fprintf(stderr, "configuración inválida: %s\n", err);
        return 1;
    }
    log_set_level(log_level_from_string(settings.log_level));

    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        LOG_E("no se pudo inicializar libcurl");
        return 1;
    }

    zc_cache_t *cache = cache_create(settings.cache_dir, (unsigned int)time(NULL) ^ (unsigned int)getpid());
    if (!cache) {
        LOG_E("no se pudo crear la caché en %s", settings.cache_dir);
        return 1;
    }

    config_store_t *configs = config_store_create();
    if (config_store_refresh(configs, &settings, err, sizeof(err)) != 0)
        LOG_W("configuración inicial no disponible (%s); se reintentará cada %d s", err,
              settings.config_refresh_seconds);
    else
        LOG_I("configuración inicial cargada");

    refresher_args_t refresher = {.store = configs, .settings = &settings};
    pthread_t refresher_thread;
    pthread_create(&refresher_thread, NULL, refresh_loop, &refresher);

    origin_options_t origin = {
        .timeout_seconds = settings.origin_timeout_seconds,
        .verify_tls = settings.origin_verify_tls,
        .max_body = settings.max_body_bytes,
    };
    proxy_t proxy = {
        .cache = cache,
        .zone = settings.zone,
        .login_url = settings.login_url,
        .session_secret = settings.session_secret,
        .fetch = origin_fetch_curl,
        .fetch_ctx = &origin,
    };
    server_ctx_t server = {.settings = &settings, .proxy = &proxy, .configs = configs};

    LOG_I("Zonal Cache (zona %s) iniciando", settings.zone);
    int rc = server_run(&server, &stop_requested);
    stop_requested = 1;
    pthread_join(refresher_thread, NULL);

    /*
     * Los hilos de conexión son desacoplados y podrían seguir usando la caché, por
     * eso no se destruye: el proceso termina y el sistema libera los recursos.
     */
    LOG_I("Zonal Cache detenida");
    curl_global_cleanup();
    return rc == 0 ? 0 : 1;
}
