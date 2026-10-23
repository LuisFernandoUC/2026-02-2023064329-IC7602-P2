# Guía de contribución

## Ramas

- `main` está protegida: solo recibe cambios por Pull Request con al menos una revisión.
- Cada tarea se trabaja en una rama que sale de `main`: `feat/<componente>`, `fix/<componente>` o `docs/<tema>`.

## Commits

Formato `tipo(alcance): descripción`, en español y en tiempo presente.

Tipos: `feat`, `fix`, `docs`, `test`, `refactor`, `chore`, `ci`.

Ejemplo: `feat(deploy): agrega chart de Helm de Apache`.

## Pull Requests

1. Abrir el PR hacia `main` usando la plantilla.
2. Indicar los IDs de tarea del reporte.
3. Esperar la revisión de otra persona del grupo y que el workflow de CI pase.
4. Unir con *squash* o *merge* y borrar la rama.

## Componentes y responsables

Ver la sección de integrantes del `README.md`.
