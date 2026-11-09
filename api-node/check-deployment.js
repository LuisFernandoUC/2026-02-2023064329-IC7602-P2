const apiUrl = process.env.API_URL || `http://${process.env.HOST}:${process.env.PORT}`;
const uiUrl = process.env.UI_URL || process.env.UI_ORIGIN;
const token = process.env.CACHE_API_TOKEN;
let api;
let ui;

try {
  api = new URL(apiUrl);
  ui = new URL(uiUrl);
  for (const url of [api, ui]) {
    if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password
      || url.pathname !== '/' || url.search || url.hash) throw new Error();
  }
  if (!token) throw new Error();
} catch {
  console.error('Configura API_URL, UI_URL y CACHE_API_TOKEN. Las URLs deben ser direcciones HTTP o HTTPS sin rutas.');
  process.exit(1);
}

async function request(path, options = {}) {
  return fetch(new URL(path, api), { ...options, signal: AbortSignal.timeout(15000) });
}

async function check(name, run) {
  try {
    await run();
    console.log(`Correcto: ${name}`);
  } catch {
    console.error(`Falló: ${name}`);
    process.exitCode = 1;
  }
}

await check('la API responde en /api/health', async () => {
  const response = await request('/api/health');
  if (response.status !== 200 || (await response.json()).status !== 'ok') throw new Error();
});

await check('la UI entrega su página', async () => {
  const response = await fetch(ui, { signal: AbortSignal.timeout(15000) });
  const html = await response.text();
  if (response.status !== 200 || !response.headers.get('Content-Type')?.includes('text/html') || !html.includes('id="root"')) throw new Error();
});

await check('CORS permite las solicitudes de esta UI', async () => {
  const response = await request('/api/domains', {
    method: 'OPTIONS',
    headers: { Origin: ui.origin, 'Access-Control-Request-Method': 'POST', 'Access-Control-Request-Headers': 'authorization,content-type' },
  });
  const allowed = response.headers.get('Access-Control-Allow-Headers')?.toLowerCase().split(',').map((value) => value.trim()) ?? [];
  if (!response.ok || response.headers.get('Access-Control-Allow-Origin') !== ui.origin
    || !allowed.includes('authorization') || !allowed.includes('content-type')) throw new Error();
});

await check('los dominios requieren una sesión del panel', async () => {
  if ((await request('/api/domains')).status !== 401) throw new Error();
});

await check('la configuración requiere el token de la caché', async () => {
  if ((await request('/api/cache/config')).status !== 401) throw new Error();
});

await check('la configuración autenticada puede leer Firestore', async () => {
  const response = await request('/api/cache/config', { headers: { Authorization: `Bearer ${token}` } });
  if (response.status !== 200 || response.headers.get('Cache-Control') !== 'no-store') throw new Error();
  const data = await response.json();
  if (!Array.isArray(data.domains)) throw new Error();
  console.log(`Dominios verificados disponibles: ${data.domains.length}`);
  if (data.domains.length === 0) console.log('Falta un dominio verificado para probar los recursos a través de la caché.');
});

await check('el login de recursos rechaza datos de prueba', async () => {
  const response = await request('/api/cache/sessions', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ domain: 'example.invalid', path: '/prueba', username: 'prueba', password: 'dato-ficticio', callback: 'https://example.invalid/__zc/auth' }),
  });
  if (response.status !== 401 || response.headers.get('Cache-Control') !== 'no-store') throw new Error();
});
