Zonal Cache - Variables de entorno
=================================

Este servicio puede redirigir a la UI y obtener configuración desde una API externa usando variables de entorno.

Variables soportadas:

- `UI_BASE_URL`: URL base de la UI (por defecto `http://localhost:5173`).
- `REDIRECT_URL`: URL a la que la UI redirige después del login (por defecto `http://localhost:8080`).
- `CONFIG_API_URL`: URL base de la API de configuración (por defecto `https://2026-01-2022437963-ic-7602.vercel.app`).

Ejemplos (PowerShell):

```powershell
$env:UI_BASE_URL = 'https://your-ui.vercel.app'
$env:REDIRECT_URL = 'https://your-ui.vercel.app'
cd proyectos/P2/zonal-cache
cargo run
```

Ejemplos (Unix / bash):

```bash
export UI_BASE_URL="https://your-ui.vercel.app"
export REDIRECT_URL="https://your-ui.vercel.app"
cd proyectos/P2/zonal-cache
cargo run
```

Notas:

- La ruta de login usada por defecto (si no se establecen variables) es `http://localhost:5173/auth?domain=...&redirect=...`.
- `CONFIG_API_URL` puede apuntar al backend que provee la configuración por dominio.
