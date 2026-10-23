# Rest API Java

Esqueleto de la Rest API en Java (T-09). Usa solo el servidor HTTP del JDK; se compila dentro del Dockerfile, sin Maven.

| Ruta | Respuesta |
| --- | --- |
| `GET /health` | `{"status":"ok"}` |
| `GET /api/hello` | `{"message":"Rest API Java del Proyecto 2"}` |

Variable de entorno: `PORT` (por defecto `8080`).

```bash
docker build -t rest-api-java rest-api-java
docker run --rm -p 8080:8080 rest-api-java
curl http://localhost:8080/health
```
