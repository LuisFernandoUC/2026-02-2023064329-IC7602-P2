mod firebase;
mod auth;
mod cache;
mod cache_core;

use cache::cache_handler;
use firebase::config::load_config;
use auth::api_key::validate_api_key;
use auth::session::{get_session_cookie, validate_session_token};

use axum::{
    extract::Request,
    http::StatusCode,
    middleware::{self, Next},
    response::{IntoResponse, Redirect, Response},
    routing::get,
    Router,
};

use std::env;

async fn auth_middleware(request: Request, next: Next) -> Result<Response, StatusCode> {
    let domain = request
        .headers()
        .get("host")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("example.com")
        .split(':')
        .next()
        .unwrap_or("example.com");

    println!("Dominio detectado: {}", domain);

    let config = match load_config(domain).await {
        Ok(config) => config,
        Err(error) => {
            println!("Error cargando configuración del dominio {}: {:?}", domain, error);
            return Err(StatusCode::SERVICE_UNAVAILABLE);
        }
    };

    if config.auth_type == "none" {
        return Ok(next.run(request).await);
    }

    if config.auth_type == "api_key" {
        if validate_api_key(request.headers(), &config.api_key) {
            return Ok(next.run(request).await);
        }

        return Err(StatusCode::UNAUTHORIZED);
    }

    if config.auth_type == "session" {
        if let Some(value) = get_session_cookie(request.headers()) {
            if validate_session_token(&value) {
                return Ok(next.run(request).await);
            }

            return Err(StatusCode::UNAUTHORIZED);
        }

        if let Some(query) = request.uri().query() {
            for param in query.split('&') {
                if let Some(token) = param.strip_prefix("token=") {
                    if validate_session_token(token) {
                        return Ok(next.run(request).await);
                    }

                    return Err(StatusCode::UNAUTHORIZED);
                }
            }
        }

        let ui_base = env::var("UI_BASE_URL").unwrap_or_else(|_| "https://ic7602-p2-ui.vercel.app".to_string());
        let redirect = env::var("REDIRECT_URL").unwrap_or_else(|_| "http://localhost:8080".to_string());

        let login_url = format!("{}/auth?domain={}&redirect={}", ui_base, domain, redirect);
        return Ok(Redirect::to(&login_url).into_response());
    }

    Err(StatusCode::UNAUTHORIZED)
}

async fn protected_resource() -> &'static str {
    "Acceso permitido según Firebase/API"
}

#[tokio::main]
async fn main() {
    dotenvy::dotenv().ok();
    let port = env::var("PORT").unwrap_or_else(|_| "8080".to_string());
    let address = format!("0.0.0.0:{}", port);

    let domain = env::var("DOMAIN").unwrap_or_else(|_| "localhost".to_string());
    let (tamanio, politica, ttl) = match load_config(&domain).await {
        Ok(config) => {
            println!("Config cargada — {}MB, {}, TTL {}s",
                config.cache_size_mb, config.replacement_policy, config.ttl);
            let bytes = std::env::var("CACHE_SIZE_BYTES")
                .ok()
                .and_then(|v| v.parse::<u64>().ok())
                .unwrap_or((config.cache_size_mb as u64) * 1024 * 1024);
            (bytes, config.replacement_policy, 300u64)
        }
        Err(e) => {
            println!("Config no disponible ({}), usando defaults", e);
            (500 * 1024 * 1024, String::from("LRU"), 300u64)
        }
    };

    let estado: cache_core::EstadoCache = std::sync::Arc::new(std::sync::Mutex::new(
        cache_core::Cache::nuevo(tamanio, cache_core::Politica::desde_string(&politica), ttl)
    ));

    let app = Router::new()
        .route("/", get(cache::cache_handler))
        .with_state(estado)
        .layer(middleware::from_fn(auth_middleware));

    let listener = tokio::net::TcpListener::bind(&address).await.unwrap();
    println!("Servidor iniciado en http://localhost:{}", port);
    axum::serve(listener, app).await.unwrap();
}

