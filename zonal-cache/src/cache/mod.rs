use axum::{
    extract::{Query, State},
    response::IntoResponse,
};
use serde::Deserialize;
use crate::cache_core::EstadoCache;

#[derive(Deserialize)]
pub struct CacheParams {
    url: Option<String>,
}

pub async fn cache_handler(
    State(estado): State<EstadoCache>,
    Query(params): Query<CacheParams>,
) -> impl IntoResponse {
    println!("Entrando al cache_handler");
    let url = params.url.unwrap_or_else(|| "https://httpbin.org/ip".to_string());
    crate::cache_core::get_cache(url, estado).await
}