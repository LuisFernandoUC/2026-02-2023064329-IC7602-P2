# Rest API Java

API simple en Java que implementa los verbos GET, POST, PUT y DELETE sobre el recurso `items`. Usa solo el servidor HTTP del JDK; se compila dentro del Dockerfile, sin Maven. Los datos se guardan en memoria.

## Endpoints

| Método | Ruta | Descripción | Respuesta |
| --- | --- | --- | --- |
| GET | `/health` | Estado del servicio | `200` `{"status":"ok"}` |
| GET | `/api/hello` | Mensaje de prueba | `200` |
| GET | `/api/items` | Lista los items | `200` |
| GET | `/api/items/{id}` | Obtiene un item | `200` o `404` |
| POST | `/api/items` | Crea un item con `{"name":"...","description":"..."}` | `201` o `400` |
| PUT | `/api/items/{id}` | Reemplaza un item | `200`, `400` o `404` |
| DELETE | `/api/items/{id}` | Elimina un item | `204` o `404` |

`name` es obligatorio y `description` es opcional.

## Variables de entorno

| Variable | Por defecto | Uso |
| --- | --- | --- |
| `PORT` | `8080` | Puerto en el que escucha |

## Ejecución

```bash
docker build -t rest-api-java rest-api-java
docker run --rm -p 8080:8080 rest-api-java
```

## Pruebas

```bash
curl -X POST -d '{"name":"uno","description":"primero"}' localhost:8080/api/items
curl localhost:8080/api/items
curl -X PUT -d '{"name":"uno-mod"}' localhost:8080/api/items/1
curl -X DELETE localhost:8080/api/items/1
```
