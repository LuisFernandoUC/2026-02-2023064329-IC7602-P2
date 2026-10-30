# Apache

Servidor Apache con HTML básico (T-09). Es el sitio de origen para probar el DNS Interceptor y la Zonal Cache.

```bash
docker build -t apache-p2 apache
docker run --rm -p 8081:80 apache-p2
curl http://localhost:8081/
```
