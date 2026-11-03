use axum::http::HeaderMap;

pub fn validate_api_key(headers: &HeaderMap, expected_key: &str) -> bool {
    let api_key = headers
        .get("x-api-key")
        .and_then(|value| value.to_str().ok());

    match api_key {
        Some(key) if key == expected_key => true,
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::http::{HeaderMap, HeaderValue};

    #[test]
    fn api_key_correcta_retorna_true() {
        let mut headers = HeaderMap::new();
        headers.insert("x-api-key", HeaderValue::from_static("abc123"));

        let result = validate_api_key(&headers, "abc123");

        assert!(result);
    }

    #[test]
    fn api_key_incorrecta_retorna_false() {
        let mut headers = HeaderMap::new();
        headers.insert("x-api-key", HeaderValue::from_static("mala"));

        let result = validate_api_key(&headers, "abc123");

        assert!(!result);
    }
}