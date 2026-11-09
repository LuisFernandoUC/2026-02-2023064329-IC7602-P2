import assert from 'node:assert/strict';
import { createHmac, generateKeyPairSync, scryptSync } from 'node:crypto';
import { after, before, beforeEach, test } from 'node:test';
import app from './app.js';
import { getDb } from './firestore.js';

process.env.FIREBASE_PROJECT_ID = 'demo-proxy-cache';
process.env.FIREBASE_CLIENT_EMAIL = 'test@demo-proxy-cache.iam.gserviceaccount.com';
process.env.FIREBASE_PRIVATE_KEY = generateKeyPairSync('rsa', {
  modulusLength: 2048,
  privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
  publicKeyEncoding: { type: 'spki', format: 'pem' },
}).privateKey;

const db = getDb();
const secret = 'secreto-de-pruebas-para-sesiones-de-recursos';
const password = ' contraseña de prueba ';
const salt = Buffer.alloc(16, 7);
const passwordHash = `scrypt$${salt.toString('hex')}$${scryptSync(password, salt, 64).toString('hex')}`;
let server;
let baseUrl;

before(async () => {
  server = app.listen(0, '127.0.0.1');
  await new Promise((resolve) => server.once('listening', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});
after(() => new Promise((resolve) => server.close(resolve)));
beforeEach(() => {
  process.env.CACHE_SESSION_SECRET = secret;
  process.env.CACHE_SESSION_SECONDS = '3600';
});

// El login lee estos documentos en memoria y no usa un proyecto Firebase real.
function mockFirestore(t, failAt) {
  const records = new Map([
    ['domains/dominio-a', { name: 'apache.local', ownerId: 'cuenta-a', verified: true }],
    ['domains/dominio-a/rules/regla-a', { pathPattern: '/privado/*', authMode: 'password' }],
    ['domains/dominio-a/rules/regla-a/users/usuario-a', { username: 'lector', passwordHash }],
  ]);
  function collection(path, filters = []) {
    return {
      where(field, operator, value) {
        assert.equal(operator, '==');
        return collection(path, [...filters, [field, value]]);
      },
      async get() {
        if (failAt && path.endsWith(failAt)) throw new Error('Detalle privado de Firestore');
        return { docs: [...records].filter(([key, data]) => key.startsWith(`${path}/`) && !key.slice(path.length + 1).includes('/') && filters.every(([field, value]) => data[field] === value))
          .map(([key, data]) => ({ id: key.split('/').at(-1), data: () => data, ref: { collection: (name) => collection(`${key}/${name}`) } })) };
      },
    };
  }
  const calls = t.mock.method(db, 'collection', collection);
  return { records, calls };
}

function input(changes = {}) {
  return { domain: 'apache.local', path: '/privado/doc.html', username: 'lector', password,
    callback: 'http://apache.local:8080/__zc/auth?next=%2Fprivado%2Fdoc.html%3Fx%3D1', ...changes };
}

function login(body = input()) {
  return fetch(`${baseUrl}/api/cache/sessions`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  });
}

function session(data) {
  const url = new URL(data.redirect);
  const token = url.searchParams.get('token');
  const [header, payload, signature] = token.split('.');
  assert.deepEqual(JSON.parse(Buffer.from(header, 'base64url')), { alg: 'HS256', typ: 'JWT' });
  assert.equal(signature, createHmac('sha256', secret).update(`${header}.${payload}`).digest('base64url'));
  return { url, payload: JSON.parse(Buffer.from(payload, 'base64url')) };
}

test('el login público valida la contraseña y devuelve un JWT limitado al dominio y la regla', async (t) => {
  mockFirestore(t);
  const now = Math.floor(Date.now() / 1000);
  const response = await login(input({ domain: ' APACHE.LOCAL ', username: ' lector ' }));
  assert.equal(response.status, 201);
  assert.equal(response.headers.get('Cache-Control'), 'no-store');
  const data = await response.json();
  assert.deepEqual(Object.keys(data).sort(), ['expiresAt', 'redirect']);
  const { url, payload } = session(data);
  assert.equal(url.origin, 'http://apache.local:8080');
  assert.equal(url.pathname, '/__zc/auth');
  assert.equal(url.searchParams.get('next'), '/privado/doc.html?x=1');
  assert.deepEqual(payload, { domain: 'apache.local', sub: 'lector', rule: 'regla-a', exp: data.expiresAt });
  assert.ok(payload.exp >= now + 3600 && payload.exp <= Math.floor(Date.now() / 1000) + 3600);
  assert.equal(JSON.stringify(data).includes(password), false);
});

test('acepta el callback HTTPS y usa la duración configurada', async (t) => {
  mockFirestore(t);
  process.env.CACHE_SESSION_SECONDS = '600';
  t.mock.method(Date, 'now', () => 1800000000000);
  const response = await login(input({ callback: 'https://apache.local/__zc/auth' }));
  assert.equal(response.status, 201);
  const { url, payload } = session(await response.json());
  assert.equal(url.protocol, 'https:');
  assert.equal(url.searchParams.get('next'), '/privado/doc.html');
  assert.equal(payload.exp, 1800000600);
});

test('rechaza credenciales incorrectas y usuarios eliminados sin distinguirlos en el mensaje', async (t) => {
  const { records } = mockFirestore(t);
  for (const changes of [{ username: 'otro' }, { password: 'incorrecta' }, { password: password.trim() }]) {
    const response = await login(input(changes));
    assert.equal(response.status, 401);
    assert.deepEqual(await response.json(), { error: 'Usuario o contraseña incorrectos.' });
  }
  records.delete('domains/dominio-a/rules/regla-a/users/usuario-a');
  assert.equal((await login()).status, 401);
});

test('no emite sesiones para dominios pendientes, eliminados o con verificación duplicada', async (t) => {
  const { records } = mockFirestore(t);
  records.get('domains/dominio-a').verified = false;
  assert.equal((await login()).status, 401);
  records.get('domains/dominio-a').verified = true;
  records.set('domains/dominio-b', { name: 'apache.local', ownerId: 'cuenta-b', verified: true });
  assert.equal((await login()).status, 401);
  records.delete('domains/dominio-a');
  records.delete('domains/dominio-b');
  assert.equal((await login()).status, 401);
});

test('elige la regla más específica entre todos los mecanismos, igual que la caché', async (t) => {
  const { records } = mockFirestore(t);
  records.set('domains/dominio-a/rules/regla-general', { pathPattern: '/*', authMode: 'password' });
  records.set('domains/dominio-a/rules/regla-general/users/otro', { username: 'otro', passwordHash });
  assert.equal((await login(input({ username: 'otro' }))).status, 401);
  const ok = await login();
  assert.equal(session(await ok.json()).payload.rule, 'regla-a');
  records.set('domains/dominio-a/rules/regla-especifica', { pathPattern: '/privado/doc.html', authMode: 'api-key' });
  assert.equal((await login()).status, 401);
});

test('los comodines y caracteres literales de las rutas coinciden con el contrato de la caché', async (t) => {
  const { records } = mockFirestore(t);
  for (const [pattern, path, status] of [['/v?/x', '/v1/x', 201], ['/v?/x', '/v10/x', 401], ['/archivo[1].html', '/archivo[1].html', 201], ['/archivo[1].html', '/archivo1Xhtml', 401]]) {
    records.get('domains/dominio-a/rules/regla-a').pathPattern = pattern;
    const response = await login(input({ path, callback: 'http://apache.local/__zc/auth' }));
    assert.equal(response.status, status);
  }
});

test('rechaza datos incompletos y callbacks externos o distintos del recurso antes de leer Firestore', async (t) => {
  const { calls } = mockFirestore(t);
  for (const changes of [{ domain: '' }, { username: ' ' }, { password: 4 }, { path: '//otro.local/x' }, { path: '/privado/doc.html?x=1' }, { path: '/con espacio' },
    ...['https://otro.local/__zc/auth', 'javascript:alert(1)', 'http://usuario:clave@apache.local/__zc/auth', 'http://apache.local/otra-ruta', 'http://apache.local/__zc/auth#externo',
      'http://apache.local/__zc/auth?next=%2F%2Fotro.local', 'http://apache.local/__zc/auth?next=%2F%5Cotro.local', 'http://apache.local/__zc/auth?next=%2Fotra-ruta'].map((callback) => ({ callback }))]) {
    const response = await login(input(changes));
    assert.equal(response.status, 400);
    assert.deepEqual(Object.keys(await response.json()), ['error']);
  }
  assert.equal((await login({})).status, 400);
  assert.equal(calls.mock.callCount(), 0);
});

test('no inicia sesiones si faltan el secreto o una duración válida', async (t) => {
  const { calls } = mockFirestore(t);
  for (const value of ['', 'corto']) {
    process.env.CACHE_SESSION_SECRET = value;
    assert.equal((await login()).status, 503);
  }
  process.env.CACHE_SESSION_SECRET = secret;
  for (const value of ['', '0', '-1', '1.5', 'incorrecto']) {
    process.env.CACHE_SESSION_SECONDS = value;
    assert.equal((await login()).status, 503);
  }
  assert.equal(calls.mock.callCount(), 0);
});

test('un hash inválido o dos usuarios con el mismo nombre no permiten iniciar sesión', async (t) => {
  const { records } = mockFirestore(t);
  const user = records.get('domains/dominio-a/rules/regla-a/users/usuario-a');
  for (const value of [undefined, 'texto', 'scrypt$aa$bb', `sha256$${salt.toString('hex')}$${'a'.repeat(128)}`]) {
    user.passwordHash = value;
    assert.equal((await login()).status, 401);
  }
  user.passwordHash = passwordHash;
  records.set('domains/dominio-a/rules/regla-a/users/duplicado', { ...user });
  assert.equal((await login()).status, 401);
});

test('los fallos de Firestore no devuelven tokens ni detalles internos', async (t) => {
  for (const failAt of ['domains', 'rules', 'users']) {
    mockFirestore(t, failAt);
    const response = await login();
    assert.equal(response.status, 503);
    const data = await response.json();
    assert.deepEqual(Object.keys(data), ['error']);
    assert.doesNotMatch(JSON.stringify(data), /Detalle privado/);
  }
});
