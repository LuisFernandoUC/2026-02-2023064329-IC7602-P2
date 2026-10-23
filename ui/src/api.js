export async function apiRequest(path, options = {}) {
  const apiUrl = import.meta.env.VITE_API_URL;

  if (!apiUrl) {
    throw new Error('Configura VITE_API_URL en ui/.env.');
  }

  const response = await fetch(new URL(path, apiUrl), options);

  if (!response.ok) {
    throw new Error(`Error de la API (${response.status}).`);
  }

  return response.status === 204 ? null : response.json();
}
