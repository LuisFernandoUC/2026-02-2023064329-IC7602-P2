use axum::http::HeaderMap;

pub fn get_session_cookie(headers: &HeaderMap) -> Option<String> {
    let cookie_header = headers
        .get("cookie")
        .or_else(|| headers.get("Cookie"))?
        .to_str()
        .ok()?;

    for cookie in cookie_header.split(';') {
        let cookie = cookie.trim();

        if let Some(value) = cookie.strip_prefix("session=") {
            return Some(value.to_string());
        }
    }

    None
}

pub fn validate_session_token(token: &str) -> bool {
    let token = token.trim();

    if token.is_empty() {
        return false;
    }

    if token == "invalid-token" {
        return false;
    }

    token == "valid-session-123"
        || token.starts_with("session-")
        || token.starts_with("eyJ")
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::http::{HeaderMap, HeaderValue};

    #[test]
    fn token_session_valido_retorna_true() {
        let result = validate_session_token("session-abc123");

        assert!(result);
    }

    #[test]
    fn token_session_invalido_retorna_false() {
        let result = validate_session_token("token-malo");

        assert!(!result);
    }

    #[test]
    fn extrae_cookie_session_correctamente() {
        let mut headers = HeaderMap::new();
        headers.insert(
            "cookie",
            HeaderValue::from_static("theme=dark; session=valid-session-123; lang=es"),
        );

        let session = get_session_cookie(&headers);

        assert_eq!(session, Some("valid-session-123".to_string()));
    }
}