FROM eclipse-temurin:21-jdk-alpine AS build
WORKDIR /app
COPY src/ src/
RUN javac -d out src/Main.java

FROM eclipse-temurin:21-jre-alpine
RUN addgroup -S app && adduser -S app -G app
WORKDIR /app
COPY --from=build /app/out/ .
USER app
ENV PORT=8080
EXPOSE 8080
CMD ["java", "-cp", ".", "Main"]
