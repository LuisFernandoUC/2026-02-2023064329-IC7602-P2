# --- Etapa de build ---
FROM gcc:14-bookworm AS build
WORKDIR /src
COPY Makefile ./
COPY src ./src
COPY data ./data
RUN make db && make CFLAGS="-O2" all

# --- Etapa de ejecución ---
FROM debian:bookworm-slim
WORKDIR /app
RUN useradd --system --no-create-home --uid 10001 interceptor
COPY --from=build /src/build/dns_interceptor ./dns_interceptor
COPY --from=build /src/data/dbip-country-lite.csv ./data/dbip-country-lite.csv
COPY data/country_zones.csv ./data/country_zones.csv

# Puerto sin privilegios: el Service de Kubernetes lo publica como UDP/53.
ENV DI_PORT=5353
EXPOSE 5353/udp

USER interceptor
ENTRYPOINT ["./dns_interceptor"]
