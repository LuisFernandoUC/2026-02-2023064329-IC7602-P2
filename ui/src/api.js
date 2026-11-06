import { getPanelAuth } from './auth.js';

export async function apiRequest(path, { panelSession = true, ...options } = {}) {
  const apiUrl = import.meta.env.VITE_API_URL;

  if (!apiUrl) {
    throw new Error('Configura VITE_API_URL en ui/.env.');
  }

  const url = new URL(path, apiUrl);
  if (url.origin !== new URL(apiUrl).origin) throw new Error('La ruta no pertenece a la API.');

  const headers = new Headers(options.headers);
  const user = panelSession ? getPanelAuth().currentUser : null;
  if (user) headers.set('Authorization', `Bearer ${await user.getIdToken()}`);

  const response = await fetch(url, { ...options, headers });

  if (!response.ok) {
    const data = await response.json().catch(() => null);
    throw new Error(data?.error || `Error de la API (${response.status}).`);
  }

  return response.status === 204 ? null : response.json();
}
