# 2026-02-2023064329-IC7602-P2

# Proyecto 2: Proxy/Caché de aplicación con geolocalización

**Curso:** Redes (IC 7602) · Escuela de Ingeniería en Computación · Tecnológico de Costa Rica · II Semestre 2026

## Tabla de contenidos

1. [Descripción general](#1-descripción-general)
2. [Integrantes](#2-integrantes)
3. [Arquitectura](#3-arquitectura)
4. [Estructura del repositorio](#4-estructura-del-repositorio)
5. [Requisitos previos](#5-requisitos-previos)
6. [Cómo ejecutar el proyecto](#6-cómo-ejecutar-el-proyecto)
7. [Configuración (variables de entorno)](#7-configuración-variables-de-entorno)
8. [Documentación por componente](#8-documentación-por-componente)
9. [Pruebas realizadas](#9-pruebas-realizadas)
10. [Estado de la implementación](#10-estado-de-la-implementación)
11. [Conclusiones](#11-conclusiones)
12. [Recomendaciones](#12-recomendaciones)
13. [Reportes de avance](#13-reportes-de-avance)
14. [Uso de inteligencia artificial generativa](#14-uso-de-inteligencia-artificial-generativa)
15. [Referencias y créditos](#15-referencias-y-créditos)

---

## 1. Descripción general

> Esta documentación está escrita para una persona con conocimientos básicos de computación: debe poder ejecutar y probar el proyecto siguiéndola paso a paso. Se recomienda apoyarse en diagramas, _snippets_ de código y capturas.

**Objetivo general:** implementar un proxy/caché de aplicación con geolocalización.

El sistema intercepta peticiones DNS de dominios específicos, las redirige a una caché HTTP zonal según el país de origen de la persona usuaria, y sirve el contenido desde disco o desde el origen. Cada dominio, URL, política de reemplazo y mecanismo de autenticación se administra desde una interfaz web.

**Objetivos específicos:**

- Desarrollar servidores UDP y TCP en GNU C.
- Procesar peticiones DNS especificadas en el RFC 2929.
- Desarrollar servicios de caché con algoritmos de reemplazo.
- Diseñar servicios de autenticación para APIs y sitios web.
- Diseñar e implementar una aplicación web con inteligencia artificial generativa.
- Implementar una API REST en Java.
- Automatizar la solución con Docker, Helm Charts, Terraform, Docker Compose, Kubernetes o proveedores de nube.

**Lenguajes y tecnologías elegidos** (el enunciado exige documentarlos; **no se permite Python**):

| Componente         | Lenguaje / tecnología        | Dónde se ejecuta     |
| ------------------ | ---------------------------- | -------------------- |
| DNS Interceptor    | GNU C                        | Kubernetes (Helm)    |
| Zonal Cache        | GNU C                        | Kubernetes (Helm)    |
| Rest API           | Java                         | Kubernetes (Helm)    |
| Apache Server      | Apache HTTP Server           | Kubernetes (Helm)    |
| REST API (Node.js) | Node.js + Express + Firebase/Firestore | Vercel               |
| UI                 | React + Vite + Tailwind CSS  | Vercel               |

## 2. Integrantes

| Carné      | Nombre                       | Componentes a cargo                                                                       |
| ---------- | ---------------------------- | ----------------------------------------------------------------------------------------- |
| 2023064329 | Luis Fernando Ureña Corrales | DNS Interceptor, Apache, Rest API Java, deploy k8s + Helm, librería de algoritmos de reemplazo |
| 2023       | Fabricio Herrera Rodriguez   | Zonal Cache                                                                               |
| 2023       | Nicole Parra Valverde        | REST API Node.js, UI y su deploy en Vercel                                                |

## 3. Arquitectura

<!-- TODO: insertar el diagrama de arquitectura del enunciado (o uno propio) en Docs/img/ y enlazarlo aquí. -->

```text
Cliente ──DNS──▶ DNS Interceptor ──(IP to Country)──▶ Zonal Cache (zona del cliente)
                                                             │
                                  ┌──────────────────────────┼─────────────────────────┐
                                  ▼                          ▼                         ▼
                           Rest API (Java)           Apache Server             Sitios externos
                                                                              (HTTP / HTTPS)

Zonal Cache ──configuración / autenticación──▶ REST API Node.js ──▶ Firebase/Firestore ◀── UI (React)
```

**Flujo de una petición:**

1. La persona usuaria consulta un dominio registrado; el **DNS Interceptor** determina su país con la base IP to Country y responde con la IP de la **Zonal Cache** de su zona.
2. La Zonal Cache obtiene su configuración desde Firebase (a través del API Node.js).
3. Verifica si la URL requiere seguridad y valida `x-api-key` o usuario/contraseña (sesión).
4. Si el recurso está en caché, lo sirve; si no, lo obtiene del origen, lo almacena (aplicando la política de reemplazo y el límite de tamaño) y lo sirve.

## 4. Estructura del repositorio

<!-- Ajustar a la estructura real cuando se cree (T-03). -->

```text
.
├── Docs/                  # Documentación adicional y reportes de avance
│   └── Reportes/
├── dns-interceptor/       # GNU C
├── zonal-cache/           # GNU C
├── rest-api-java/         # Java
├── apache/                # Apache + HTML básico
├── api-node/              # Node.js (Vercel)
├── ui/                    # React + Vite + Tailwind (Vercel)
├── deploy/                # Helm Charts, Docker Compose, scripts
└── README.md
```

## 5. Requisitos previos

<!-- Completar con las versiones usadas. -->

| Herramienta                    | Versión | Para qué se usa                          |
| ------------------------------ | ------- | ---------------------------------------- |
| Docker                         | _TODO_  | Construir imágenes                       |
| Kubernetes (minikube/kind/...) | _TODO_  | Cluster local                            |
| Helm                           | _TODO_  | Instalar los charts                      |
| GCC / Make                     | _TODO_  | Compilar DNS Interceptor y Zonal Cache   |
| JDK + Maven/Gradle             | _TODO_  | Compilar la Rest API Java                |
| Node.js                        | 24.x para la API; UI pendiente | API Node.js y UI                         |
| `nslookup`, `curl` / Postman   | —       | Probar el sistema                        |

## 6. Cómo ejecutar el proyecto

> **Toda la instalación debe estar automatizada.** Un componente sin automatizar se califica con 0.

### 6.1 Clonar el repositorio

```bash
git clone <URL_DEL_REPOSITORIO>
cd 2026-02-2023064329-IC7602-P2
```

### 6.2 Configurar variables de entorno

No hay valores quemados en el código. Ver [sección 7](#7-configuración-variables-de-entorno).

### 6.3 Construir las imágenes

<!-- TODO: comandos de build de cada componente. -->

### 6.4 Levantar el cluster y desplegar con Helm

<!-- TODO: comando(s) para crear el cluster local e instalar el chart paraguas o los charts por componente. -->

### 6.5 Desplegar el API Node.js y la UI (Vercel)

<!-- TODO: pasos de conexión del repositorio de GitHub con Vercel y variables de entorno requeridas. -->

### 6.6 Verificar que todo funciona

<!-- TODO: comandos de verificación (kubectl get pods, nslookup, curl). -->

### 6.7 Desinstalar / limpiar

<!-- TODO: helm uninstall, eliminación del cluster. -->

## 7. Configuración (variables de entorno)

Toda configuración se inyecta por variables de entorno. Documentar aquí cada una, por componente.

| Componente      | Variable | Descripción | Ejemplo / valor por defecto |
| --------------- | -------- | ----------- | --------------------------- |
| DNS Interceptor | _TODO_   |             |                             |
| Zonal Cache     | _TODO_   |             |                             |
| Rest API Java   | _TODO_   |             |                             |
| API Node.js     | `HOST`   | Dirección del servidor local | `127.0.0.1` |
| API Node.js     | `PORT`   | Puerto del servidor local | `3000` |
| API Node.js     | `FIREBASE_PROJECT_ID` | ID del proyecto de Firebase | `project_id` de la cuenta de servicio |
| API Node.js     | `FIREBASE_CLIENT_EMAIL` | Correo de la cuenta de servicio | `client_email` |
| API Node.js     | `FIREBASE_PRIVATE_KEY` | Clave privada de la cuenta de servicio | `private_key`, entre comillas dobles |
| UI              | _TODO_   |             |                             |

## 8. Documentación por componente

La documentación debe cubrir **todos** los componentes implementados, instalados o configurados. Un componente no documentado cuenta como no implementado.

### 8.1 DNS Interceptor

<!-- Qué hace, cómo se compila, cómo se configura, cómo se despliega (Helm), cómo se prueba con nslookup. Incluir diagrama de flujo. -->

### 8.2 Zonal Cache

<!-- Servidor HTTP, caché en disco y límite por dominio, algoritmos de reemplazo (LRU, LFU, FIFO, MRU, Random), configuración desde Firebase, autenticación (x-api-key y usuario/contraseña con sesión), SSL termination, despliegue (Helm). -->

### 8.3 Rest API (Java)

<!-- Endpoints GET, POST, PUT y DELETE, cómo se compila y despliega. -->

### 8.4 Apache Server

<!-- Página HTML básica, imagen y despliegue. -->

### 8.5 REST API Node.js

La base de la API incluye `GET /api/health`. Ver [cómo ejecutarla y probarla](api-node/README.md).

La conexión a Firestore se configura con variables de entorno. El comando `npm run check:firestore`, desde `api-node/`, permite comprobar el acceso. Ver [configuración de Firestore](api-node/README.md#configurar-firestore).

Propuesta de [datos y endpoints iniciales](Docs/Contratos/API-Node.md), con los acuerdos pendientes para integrar la UI y las cachés zonales.

<!-- Endpoints, mecanismo de autenticación y autorización elegido (investigado y justificado), verificación de dominios por registro TXT, deploy en Vercel. -->

### 8.6 UI

<!-- Registro, administración de dominios y URLs (wildcards), tamaño de caché, tipos de archivo con TTL y política, mecanismo de autenticación, formulario de login para las cachés, cierre de sesión. Deploy en Vercel. Herramienta de IA generativa utilizada. -->

### 8.7 Automatización

<!-- Dockerfiles, Docker Compose, Helm Charts, Terraform (si aplica). -->

## 9. Pruebas realizadas

Cada prueba debe incluir los pasos para reproducirla. El sistema debe probarse con:

| #   | Prueba                                           | Pasos para reproducir | Resultado esperado | Resultado obtenido |
| --- | ------------------------------------------------ | --------------------- | ------------------ | ------------------ |
| 1   | Rest API (Java)                                  | _TODO_                |                    |                    |
| 2   | Apache con HTML simple                           | _TODO_                |                    |                    |
| 3   | Sitio externo con **HTTP**                       | _TODO_                |                    |                    |
| 4   | Sitio externo con **HTTPS**                      | _TODO_                |                    |                    |
| 5   | Resolución DNS con `nslookup` (geolocalización)  | _TODO_                |                    |                    |
| 6   | Autenticación con `x-api-key`                    | _TODO_                |                    |                    |
| 7   | Autenticación con usuario y contraseña (sesión)  | _TODO_                |                    |                    |
| 8   | Algoritmos de reemplazo (LRU, LFU, FIFO, MRU, Random) | _TODO_           |                    |                    |
| 9   | Límite de tamaño de caché por dominio            | _TODO_                |                    |                    |
| 10  | Conexión API Node.js con Firestore | Desde `api-node/`, configurar `.env` y ejecutar `npm run check:firestore`. | `Conexión con Firestore verificada.` | Correcto; consulta de lectura realizada. |

Recomendación del enunciado: usar `nslookup` para el DNS Interceptor y Postman/`curl` para la caché zonal.

## 10. Estado de la implementación

Resumen de qué funciona y qué no, para cada aspecto requerido por el enunciado.

| Aspecto                  | Funciona | No funciona / limitaciones | Notas |
| ------------------------ | -------- | -------------------------- | ----- |
| DNS Interceptor          | _TODO_   | _TODO_                     |       |
| Zonal Cache              | _TODO_   | _TODO_                     |       |
| RestAPI Node.js / UI     | _TODO_   | _TODO_                     |       |
| Rest API / Apache Server | _TODO_   | _TODO_                     |       |

## 11. Conclusiones

> Se requieren **al menos 10**.

1. _TODO_
2. _TODO_
3. _TODO_
4. _TODO_
5. _TODO_
6. _TODO_
7. _TODO_
8. _TODO_
9. _TODO_
10. _TODO_

## 12. Recomendaciones

> Se requieren **al menos 10**.

1. _TODO_
2. _TODO_
3. _TODO_
4. _TODO_
5. _TODO_
6. _TODO_
7. _TODO_
8. _TODO_
9. _TODO_
10. _TODO_

## 13. Reportes de avance

Los reportes semanales se encuentran en [`Docs/Reportes/`](Docs/Reportes/).

| Reporte | Fecha      | Archivo                                |
| ------- | ---------- | -------------------------------------- |
| P2R1    | 15/10/2026 | [R1.md](Docs/Reportes/R1.md)           |

## 14. Uso de inteligencia artificial generativa

El uso de IA está regulado por el documento _Política para el Uso de Inteligencia Artificial Generativa_.

<!-- Documentar qué herramientas se usaron, para qué partes (por ejemplo, la UI) y cómo se verificó el resultado. -->

## 15. Referencias y créditos

El enunciado exige reconocer al autor de cualquier código o configuración tomado de Internet; usar código sin reconocimiento se considera copia.

| Recurso | Autor / fuente | Dónde se usa |
| ------- | -------------- | ------------ |
| _TODO_  |                |              |

---

**Fechas importantes:** entrega el **03/11/2026** antes de medianoche (tar.gz + enlace al repositorio) · evaluación presencial el **10/11/2026**.
