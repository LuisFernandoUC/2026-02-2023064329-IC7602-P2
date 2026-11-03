use axum::{routing::{get, post}, Router, extract::Query, response::IntoResponse, http::StatusCode};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use serde::Deserialize;
use std::fs;
use std::time::{SystemTime, UNIX_EPOCH};
use crate::firebase::config::load_config;
use std::path::Path;
// ── Política de reemplazo ─────────────────────────────────────────────────────
#[derive(Clone, Debug)]
pub enum Politica {
    FIFO,
    LRU,
    MRU,
    LFU,
    Random,
}

impl Politica {
    pub fn desde_string(s: &str) -> Politica {
        match s {
            "LRU"    => Politica::LRU,
            "MRU"    => Politica::MRU,
            "LFU"    => Politica::LFU,
            "Random" => Politica::Random,
            _        => Politica::FIFO,
        }
    }
}

// ── Entrada del caché ─────────────────────────────────────────────────────────
#[derive(Clone)]
struct EntradaCache {
    ruta_en_disco:   String,
    tamanio_bytes:   u64,
    insertado_en:    u64,   // timestamp — para FIFO
    ultimo_acceso:   u64,   // timestamp — para LRU y MRU
    frecuencia:      u64,   // contador   — para LFU
    ttl:            u64, // timestamp de expiración 
}

// ── Estado del caché ──────────────────────────────────────────────────────────
pub struct Cache {
    pub entradas:        HashMap<String, EntradaCache>,
    pub tamanio_actual:  u64,
    pub tamanio_maximo:  u64,   // en bytes — vendrá de Firebase vía Persona 3
    pub politica:        Politica,
    pub ttl:           u64, // duración en segundos antes de que una entrada expire
}

impl Cache {
    pub fn nuevo(tamanio_maximo: u64, politica: Politica, ttl: u64) -> Cache {
        Cache {
            entradas: HashMap::new(),
            tamanio_actual: 0,
            tamanio_maximo,
            politica,
            ttl,
        }
    }

    #[cfg(test)]
    fn insertar_directo(&mut self, url: String, tamanio: u64) {
        while self.tamanio_actual + tamanio > self.tamanio_maximo && !self.entradas.is_empty() {
            self.evictar();
        }
        let ahora = Self::ahora();
        self.entradas.insert(url, EntradaCache {
            ruta_en_disco: String::from(""),
            tamanio_bytes: tamanio,
            insertado_en:  ahora,
            ultimo_acceso: ahora,
            frecuencia:    1,
            ttl:           self.ttl,
        });
        self.tamanio_actual += tamanio;
    }    

    pub fn ahora() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs()
    }

    // Buscar una entrada y actualizar su metadata
    fn get(&mut self, url: &str) -> Option<EntradaCache> {
        let ahora = Self::ahora();

        if let Some(entrada) = self.entradas.get(url) {
            if ahora > entrada.insertado_en + entrada.ttl {
                println!("TTL expirado — eliminando: {}", url);

                let entrada = self.entradas.remove(url).unwrap();
                self.tamanio_actual -= entrada.tamanio_bytes;
                let _ = fs::remove_file(&entrada.ruta_en_disco);

                return None;
            }
        }

        if let Some(entrada) = self.entradas.get_mut(url) {
            entrada.ultimo_acceso = ahora;
            entrada.frecuencia += 1;
            return Some(entrada.clone());
        }

        None
    }

    // Insertar nueva entrada, aplicando reemplazo si es necesario
    fn insertar(&mut self, url: String, ruta: String, tamanio: u64) {
        // Si ya existe, actualizar
        if self.entradas.contains_key(&url) {
            self.tamanio_actual -= self.entradas[&url].tamanio_bytes;
        }

        // Mientras no haya espacio, aplicar política de reemplazo
        while self.tamanio_actual + tamanio > self.tamanio_maximo && !self.entradas.is_empty() {
            self.evictar();
        }

        let ahora = Self::ahora();
        self.entradas.insert(url, EntradaCache {
            ruta_en_disco: ruta,
            tamanio_bytes: tamanio,
            insertado_en:  ahora,
            ultimo_acceso: ahora,
            frecuencia:    1,
            ttl:           self.ttl,
        });
        self.tamanio_actual += tamanio;
    }

    // Decidir cuál entrada borrar según la política
    fn evictar(&mut self) {
        let url_a_borrar = match self.politica {

            Politica::FIFO => self.entradas
                .iter()
                .min_by_key(|(_, e)| e.insertado_en)
                .map(|(k, _)| k.clone()),

            Politica::LRU => self.entradas
                .iter()
                .min_by_key(|(_, e)| e.ultimo_acceso)
                .map(|(k, _)| k.clone()),

            Politica::MRU => self.entradas
                .iter()
                .max_by_key(|(_, e)| e.ultimo_acceso)
                .map(|(k, _)| k.clone()),

            Politica::LFU => self.entradas
                .iter()
                .min_by_key(|(_, e)| e.frecuencia)
                .map(|(k, _)| k.clone()),

            Politica::Random => {
                let keys: Vec<String> = self.entradas.keys().cloned().collect();
                let idx = Self::ahora() as usize % keys.len();
                Some(keys[idx].clone())
            }
        };

        if let Some(url) = url_a_borrar {
            if let Some(entrada) = self.entradas.remove(&url) {
                self.tamanio_actual -= entrada.tamanio_bytes;
                let _ = fs::remove_file(&entrada.ruta_en_disco);
                println!("EVICT [{:?}] — borrado: {}", self.politica, url);
            }
        }
    }
}

pub type EstadoCache = Arc<Mutex<Cache>>;

// ── Handler HTTP ──────────────────────────────────────────────────────────────
#[derive(Deserialize)]
struct Params {
    url: String,
}

async fn buscar_en_cache(
    Query(params): Query<Params>,
    axum::extract::State(cache): axum::extract::State<EstadoCache>,
) -> impl IntoResponse {
    let url = params.url.clone();

    // HIT
    {
        let mut cache = cache.lock().unwrap();
        if let Some(entrada) = cache.get(&url) {
            let contenido = fs::read(&entrada.ruta_en_disco).unwrap_or_default();
            println!("HIT — sirviendo desde disco: {}", entrada.ruta_en_disco);
            return (StatusCode::OK, String::from_utf8_lossy(&contenido).to_string());
        }
    }

    // MISS — ir al origen
    println!("MISS — buscando en origen: {}", url);
    match reqwest::get(&url).await {
        Ok(respuesta) => {
            let contenido = respuesta.text().await.unwrap_or_default();

            fs::create_dir_all("cache_disco").unwrap();

            let nombre_archivo = url
                .replace("http://", "")
                .replace("https://", "")
                .replace("/", "_")
                .replace(":", "_");

            let ruta = format!("cache_disco/{}", nombre_archivo);
            fs::write(&ruta, &contenido).unwrap();
            let tamanio = contenido.len() as u64;

            println!("Guardado en disco: {} ({} bytes)", ruta, tamanio);

            let mut cache = cache.lock().unwrap();
            cache.insertar(url, ruta, tamanio);

            (StatusCode::OK, contenido)
        }
        Err(e) => {
            println!("Error: {}", e);
            (StatusCode::BAD_GATEWAY, format!("Error: {}", e))
        }
    }
}


async fn api_exists(
    Query(params): Query<Params>,
    axum::extract::State(cache): axum::extract::State<EstadoCache>,
) -> impl IntoResponse {
    let mut cache = cache.lock().unwrap();

    match cache.get(&params.url) {
        Some(entrada) => {
            let contenido = fs::read(&entrada.ruta_en_disco).unwrap_or_default();
            (StatusCode::OK, String::from_utf8_lossy(&contenido).to_string())
        }
        None => (StatusCode::NOT_FOUND, format!("No existe en caché: {}", params.url))
    }
}

#[derive(Deserialize)]
struct DnsRequest {
    data: String,
}

async fn api_dns_resolver(
    axum::extract::Json(body): axum::extract::Json<DnsRequest>,
) -> impl IntoResponse {
    let dns_interceptor_url = std::env::var("DNS_INTERCEPTOR_URL")
        .unwrap_or_else(|_| "http://localhost:5000".to_string());

    let client = reqwest::Client::new();
    match client
        .post(format!("{}/resolve", dns_interceptor_url))
        .json(&serde_json::json!({ "data": body.data }))
        .send()
        .await
    {
        Ok(respuesta) => {
            let texto = respuesta.text().await.unwrap_or_default();
            (StatusCode::OK, texto)
        }
        Err(e) => (StatusCode::BAD_GATEWAY, format!("Error: {}", e))
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────
#[tokio::main]
async fn main() {
    let domain = std::env::var("DOMAIN")
        .unwrap_or_else(|_| "localhost".to_string());

    println!("Cargando configuración para dominio: {}", domain);

    let (tamanio_maximo, politica, ttl) = match load_config(&domain).await {
        Ok(config) => {
            println!("Config cargada — tamaño: {}MB, política: {}", 
                config.cache_size_mb, config.replacement_policy);
            let bytes = (config.cache_size_mb as u64) * 1024 * 1024;
            let politica = Politica::desde_string(&config.replacement_policy);
            let ttl = config.ttl as u64;
            (bytes, politica, ttl)
        }
        Err(e) => {
            println!("No se pudo cargar config ({}), usando valores por defecto", e);
            (500 * 1024 * 1024, Politica::LRU, 3600) // ttl por defecto de 1 hora
        }
    };

    let cache = Cache::nuevo(tamanio_maximo, politica, ttl);
    let estado: EstadoCache = Arc::new(Mutex::new(cache));

    let app = Router::new()
        .route("/cache", get(buscar_en_cache))
        .route("/api/exists", get(api_exists))
        .route("/api/dns_resolver", post(api_dns_resolver))
        .with_state(estado);

    let listener = tokio::net::TcpListener::bind("0.0.0.0:3001").await.unwrap();
    println!("Servidor corriendo en http://localhost:3001");
    axum::serve(listener, app).await.unwrap();
}



#[cfg(test)]
mod tests {
    use super::*;

    // ── FIFO ─────────────────────────────────────────────────────────────────
    #[test]
    fn fifo_borra_el_primero_en_llegar() {
        let mut cache = Cache::nuevo(100, Politica::FIFO, 3600);
        cache.insertar_directo(String::from("url_a"), 60);
        cache.insertar_directo(String::from("url_b"), 60); // fuerza eviction

        // url_a llegó primero, debe haberse borrado
        assert!(!cache.entradas.contains_key("url_a"), "FIFO debió borrar url_a");
        assert!(cache.entradas.contains_key("url_b"),  "FIFO debió conservar url_b");
    }

    #[test]
    fn fifo_ignora_accesos_recientes() {
        let mut cache = Cache::nuevo(100, Politica::FIFO, 3600);
        cache.insertar_directo(String::from("url_a"), 60);

        // acceder a url_a — FIFO no debería importarle
        cache.get("url_a");
        cache.get("url_a");

        cache.insertar_directo(String::from("url_b"), 60); // fuerza eviction

        // url_a debe borrarse igual aunque se accedió recientemente
        assert!(!cache.entradas.contains_key("url_a"), "FIFO debe ignorar accesos recientes");
        assert!(cache.entradas.contains_key("url_b"),  "FIFO debe conservar url_b");
    }

    // ── LRU ──────────────────────────────────────────────────────────────────
    #[test]
    fn lru_borra_el_menos_reciente() {
        let mut cache = Cache::nuevo(100, Politica::LRU, 3600);

        cache.insertar_directo(String::from("url_a"), 40);
        cache.insertar_directo(String::from("url_b"), 40);

        if let Some(entrada) = cache.entradas.get_mut("url_a") {
            entrada.ultimo_acceso = 200;
        }

        if let Some(entrada) = cache.entradas.get_mut("url_b") {
            entrada.ultimo_acceso = 100;
        }

        cache.insertar_directo(String::from("url_c"), 40);

        assert!(!cache.entradas.contains_key("url_b"), "LRU debió borrar url_b");
        assert!(cache.entradas.contains_key("url_a"), "LRU debió conservar url_a");
    }

    // ── MRU ──────────────────────────────────────────────────────────────────
    #[test]
    fn mru_borra_el_mas_reciente() {
        let mut cache = Cache::nuevo(100, Politica::MRU, 3600);
        cache.insertar_directo(String::from("url_a"), 40);
        cache.insertar_directo(String::from("url_b"), 40);

        // forzar ultimo_acceso de url_a a un valor mayor manualmente
        if let Some(entrada) = cache.entradas.get_mut("url_a") {
            entrada.ultimo_acceso = Cache::ahora() + 1000;
        }

        cache.insertar_directo(String::from("url_c"), 40); // fuerza eviction

        // url_a debe borrarse porque tiene ultimo_acceso mayor
        assert!(!cache.entradas.contains_key("url_a"), "MRU debió borrar url_a");
        assert!(cache.entradas.contains_key("url_b"),  "MRU debió conservar url_b");
    }

    // ── LFU ──────────────────────────────────────────────────────────────────
    #[test]
    fn lfu_borra_el_menos_frecuente() {
        let mut cache = Cache::nuevo(100, Politica::LFU, 3600);
        cache.insertar_directo(String::from("url_a"), 40);
        cache.insertar_directo(String::from("url_b"), 40);

        // acceder a url_a varias veces para aumentar su frecuencia
        cache.get("url_a");
        cache.get("url_a");
        cache.get("url_a");

        cache.insertar_directo(String::from("url_c"), 40); // fuerza eviction

        // url_b debe borrarse porque tiene frecuencia 1, url_a tiene 4
        assert!(!cache.entradas.contains_key("url_b"), "LFU debió borrar url_b");
        assert!(cache.entradas.contains_key("url_a"),  "LFU debió conservar url_a");
    }

    // ── Random ───────────────────────────────────────────────────────────────
    #[test]
    fn random_borra_alguna_entrada() {
        let mut cache = Cache::nuevo(100, Politica::Random, 3600);
        cache.insertar_directo(String::from("url_a"), 60);
        cache.insertar_directo(String::from("url_b"), 60); // fuerza eviction

        // no sabemos cuál borró, pero el tamaño debe ser correcto
        assert_eq!(cache.entradas.len(), 1, "Random debió conservar exactamente 1 entrada");
        assert!(cache.tamanio_actual <= 100, "Random no debe exceder el tamaño máximo");
    }
}
pub async fn get_cache(
    url: String,
    estado: EstadoCache,
) -> String {
    // ===== HIT =====
    {
        let mut cache = estado.lock().unwrap();
        if let Some(entrada) = cache.get(&url) {
            let contenido = fs::read_to_string(&entrada.ruta_en_disco)
                .unwrap_or_else(|_| "Error leyendo caché".to_string());
            println!("HIT — sirviendo desde disco: {}", entrada.ruta_en_disco);
            return contenido;
        }
    }

    // ===== MISS =====
    println!("MISS — buscando en origen: {}", url);
    match reqwest::get(&url).await {
        Ok(res) => {
            match res.text().await {
                Ok(contenido) => {
                    fs::create_dir_all("cache_disco").unwrap();

                    let nombre_archivo = url
                        .replace("http://", "")
                        .replace("https://", "")
                        .replace("/", "_")
                        .replace(":", "_");
                    let ruta = format!("cache_disco/{}", nombre_archivo);

                    fs::write(&ruta, &contenido).unwrap();
                    let tamanio = contenido.len() as u64;
                    println!("Guardado en disco: {} ({} bytes)", ruta, tamanio);

                    let mut cache = estado.lock().unwrap();
                    cache.insertar(url, ruta, tamanio);

                    contenido
                }
                Err(_) => "Error leyendo respuesta".to_string()
            }
        }
        Err(_) => "Error consultando origen".to_string()
    }
}