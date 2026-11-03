# Zonal Cache - imagen multi-etapa
#
#   docker build -t zonal-cache ./zonal-cache                 # imagen de ejecución
#   docker build --target test -t zonal-cache-test ./zonal-cache  # corre las pruebas unitarias

# ---------- compilación ----------
FROM debian:bookworm-slim AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential libcurl4-openssl-dev libssl-dev libcjson-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY Makefile ./
COPY src ./src
COPY tests ./tests
RUN make

# ---------- pruebas unitarias ----------
FROM build AS test
RUN make test

# ---------- ejecución ----------
FROM debian:bookworm-slim AS runtime
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       libcurl4 libssl3 libcjson1 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --home /nonexistent zonalcache \
    && mkdir -p /var/cache/zonal-cache \
    && chown zonalcache /var/cache/zonal-cache
COPY --from=build /app/bin/zonal-cache /usr/local/bin/zonal-cache
USER zonalcache
EXPOSE 8080 8443
ENTRYPOINT ["/usr/local/bin/zonal-cache"]
