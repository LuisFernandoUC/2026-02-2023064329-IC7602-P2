import assert from 'node:assert/strict';
import { createHash, generateKeyPairSync } from 'node:crypto';
import { after, before, test } from 'node:test';
import { getAuth } from 'firebase-admin/auth';
import app from './app.js';
import { getFirebaseApp } from './firebase.js';
import { getDb } from './firestore.js';

process.env.FIREBASE_PROJECT_ID = 'demo-proxy-cache';
process.env.FIREBASE_CLIENT_EMAIL = 'test@demo-proxy-cache.iam.gserviceaccount.com';
process.env.FIREBASE_PRIVATE_KEY = generateKeyPairSync('rsa', {
  modulusLength: 2048,
  privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
  publicKeyEncoding: { type: 'spki', format: 'pem' },
}).privateKey;

const db = getDb();
const auth = getAuth(getFirebaseApp());
let server;
let baseUrl;

before(async () => {
  server = app.listen(0, '127.0.0.1');
  await new Promise((resolve) => server.once('listening', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});
after(() => new Promise((resolve) => server.close(resolve)));

// Los documentos y las credenciales de estas pruebas solo existen en memoria.
function mockFirebase(t, failAt) {
  const records = new Map([
    ['domains/dominio-a', { ownerId: 'cuenta-a', name: 'apache.local', maxCacheBytes: 1048576, originUrl: 'http://apache-apache', verified: true }],
    ['domains/dominio-b', { ownerId: 'cuenta-b', name: 'otro.local', maxCacheBytes: 1048576, verified: false }],
  ]);
  let nextId = 0;
  function document(path) {
    return {
      id: path.split('/').at(-1), path,
      get: async () => ({ exists: records.has(path), data: () => records.get(path) }),
      collection: (name) => collection(`${path}/${name}`),
      async create(data) {
        if (failAt === 'key-create' && path.includes('/keys/')) throw new Error('Detalle privado de Firestore');
        assert.equal(records.has(path), false);
        records.set(path, data);
      },
      async update(data) { assert.ok(records.has(path)); records.set(path, { ...records.get(path), ...data }); },
      async delete() {
        if (failAt === 'key-delete') throw new Error('Detalle privado de Firestore');
        records.delete(path);
      },
    };
  }
  function collection(path) {
    const read = async (filter = () => true) => {
      if (failAt === 'keys-read' && path.endsWith('/keys')) throw new Error('Detalle privado de Firestore');
      return {
        docs: [...records].filter(([key, data]) => key.startsWith(`${path}/`) && !key.slice(path.length + 1).includes('/') && filter(data))
          .map(([key, data]) => ({ id: document(key).id, ref: document(key), data: () => data })),
      };
    };
    return {
      doc: (id = `regla-${++nextId}`) => document(`${path}/${id}`),
      get: () => read(),
      where: (field, operator, value) => {
        assert.equal(operator, '==');
        return { get: () => read((data) => data[field] === value) };
      },
    };
  }
  t.mock.method(auth, 'verifyIdToken', async (token) => ({ uid: token }));
  t.mock.method(db, 'collection', collection);
  t.mock.method(db, 'recursiveDelete', async (ref) => {
    for (const key of records.keys()) if (key === ref.path || key.startsWith(`${ref.path}/`)) records.delete(key);
  });
  return records;
}

function validRule(changes = {}) {
  return { pathPattern: '/prueba/*', maxCacheBytes: 524288, authMode: 'api-key', fileTypes: [{ extension: 'html', ttlSeconds: 60, replacementPolicy: 'LRU' }], ...changes };
}

function request(method, path = '', body, domain = 'dominio-a', token = 'cuenta-a') {
  return fetch(`${baseUrl}/api/domains/${encodeURIComponent(domain)}/rules${path}`, {
    method, headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
}

test('todas las operaciones de reglas requieren sesión', async () => {
  for (const [method, path] of [['GET', ''], ['POST', ''], ['PUT', '/regla-a'], ['DELETE', '/regla-a'], ['GET', '/regla-a/keys'], ['POST', '/regla-a/keys'], ['DELETE', '/regla-a/keys/clave-a']]) {
    const response = await fetch(`${baseUrl}/api/domains/dominio-a/rules${path}`, { method });
    assert.equal(response.status, 401);
  }
});

test('otra cuenta no puede leer, crear, modificar ni eliminar reglas del dominio', async (t) => {
  const records = mockFirebase(t);
  records.set('domains/dominio-a/rules/regla-a', validRule());
  const original = [...records];
  for (const [method, path] of [['GET', ''], ['POST', ''], ['PUT', '/regla-a'], ['DELETE', '/regla-a'], ['GET', '/regla-a/keys'], ['POST', '/regla-a/keys'], ['DELETE', '/regla-a/keys/clave-a']]) {
    const response = await request(method, path, method === 'POST' || method === 'PUT' ? validRule() : undefined, 'dominio-a', 'cuenta-b');
    assert.equal(response.status, 404);
  }
  assert.deepEqual([...records], original);
});

test('guarda los campos de la regla y los conserva al consultar la lista', async (t) => {
  const records = mockFirebase(t);
  const response = await request('POST', '', validRule({ pathPattern: ' /prueba/* ', fileTypes: [{ extension: ' .HTML ', ttlSeconds: 0, replacementPolicy: 'Random' }], id: 'id-del-cliente', ownerId: 'otra-cuenta', keys: ['no-guardar'] }));
  assert.equal(response.status, 201);
  const rule = await response.json();
  assert.deepEqual(rule, { id: 'regla-1', ...validRule({ fileTypes: [{ extension: 'html', ttlSeconds: 0, replacementPolicy: 'Random' }] }) });
  assert.equal(records.has('domains/dominio-a/rules/regla-1'), true);
  const listed = await request('GET');
  assert.deepEqual(await listed.json(), [rule]);
  const other = await request('GET', '', undefined, 'dominio-b', 'cuenta-b');
  assert.deepEqual(await other.json(), []);
});

test('valida las reglas antes de escribir en Firestore', async (t) => {
  const records = mockFirebase(t);
  for (const changes of [
    { pathPattern: 'sin-barra' }, { pathPattern: '/con espacio' },
    { maxCacheBytes: 0 }, { maxCacheBytes: 1.5 }, { maxCacheBytes: Number.MAX_SAFE_INTEGER + 1 }, { maxCacheBytes: 2097152 },
    { authMode: 'sin-validar' }, { fileTypes: null }, { fileTypes: [] }, { fileTypes: [null] },
    { fileTypes: [{ extension: '../html', ttlSeconds: 60, replacementPolicy: 'LRU' }] },
    { fileTypes: [validRule().fileTypes[0], { ...validRule().fileTypes[0], extension: '.HTML' }] },
    { fileTypes: [{ extension: 'html', ttlSeconds: -1, replacementPolicy: 'LRU' }] },
    { fileTypes: [{ extension: 'html', ttlSeconds: 1.5, replacementPolicy: 'LRU' }] },
    { fileTypes: [{ extension: 'html', ttlSeconds: 60, replacementPolicy: 'otra' }] },
  ]) {
    const response = await request('POST', '', validRule(changes));
    assert.equal(response.status, 400);
  }
  assert.equal(records.size, 2);
});

test('editar conserva el identificador y las subcolecciones de la regla', async (t) => {
  const records = mockFirebase(t);
  records.set('domains/dominio-a/rules/regla-a', validRule());
  records.set('domains/dominio-a/rules/regla-a/keys/clave-a', { keyHash: 'hash-existente' });
  const changed = validRule({ pathPattern: '/editada/*', authMode: 'password' });
  const response = await request('PUT', '/regla-a', changed);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { id: 'regla-a', ...changed });
  assert.deepEqual(records.get('domains/dominio-a/rules/regla-a'), changed);
  assert.ok(records.has('domains/dominio-a/rules/regla-a/keys/clave-a'));
});

test('eliminar una regla borra sus subcolecciones y conserva las demás reglas', async (t) => {
  const records = mockFirebase(t);
  for (const path of ['regla-a', 'regla-a/keys/clave-a', 'regla-a/users/usuario-a', 'regla-a/sessions/sesion-a', 'regla-b']) {
    records.set(`domains/dominio-a/rules/${path}`, validRule());
  }
  const response = await request('DELETE', '/regla-a');
  assert.equal(response.status, 204);
  assert.equal(await response.text(), '');
  assert.ok(records.has('domains/dominio-a/rules/regla-b'));
  assert.equal([...records.keys()].some((path) => path.includes('/regla-a')), false);
});

test('rechaza dominios o reglas inexistentes y rutas de documento inválidas', async (t) => {
  const records = mockFirebase(t);
  for (const response of [
    await request('POST', '', validRule(), 'inexistente'),
    await request('GET', '', undefined, 'ruta/no/valida'),
    await request('PUT', '/inexistente', validRule()),
    await request('DELETE', '/inexistente'),
    await request('PUT', '/ruta%2Fno%2Fvalida', validRule()),
    await request('DELETE', '/ruta%2Fno%2Fvalida'),
  ]) assert.equal(response.status, 404);
  assert.equal(records.size, 2);
});

test('una regla guardada llega al formato que consulta la caché', async (t) => {
  mockFirebase(t);
  process.env.CACHE_API_TOKEN = 'token-local-de-cache';
  const created = await request('POST', '', validRule());
  const rule = await created.json();
  const response = await fetch(`${baseUrl}/api/cache/config`, { headers: { Authorization: 'Bearer token-local-de-cache' } });
  assert.equal(response.status, 200);
  const config = await response.json();
  assert.deepEqual(config.domains[0].rules, [{ ...rule, apiKeys: [] }]);
});

test('los fallos de Firestore no exponen detalles ni confirman operaciones fallidas', async (t) => {
  mockFirebase(t);
  t.mock.method(db, 'collection', () => { throw new Error('Detalle privado de Firestore'); });
  for (const response of [await request('GET'), await request('POST', '', validRule()), await request('PUT', '/regla-a', validRule()), await request('DELETE', '/regla-a')]) {
    assert.equal(response.status, 503);
    assert.doesNotMatch(JSON.stringify(await response.json()), /Detalle privado/);
  }
});

function cacheConfig() {
  process.env.CACHE_API_TOKEN = 'token-pruebas-claves';
  return fetch(`${baseUrl}/api/cache/config`, { headers: { Authorization: 'Bearer token-pruebas-claves' } });
}

test('genera claves distintas y guarda solo sus hashes; el listado muestra únicamente IDs', async (t) => {
  const records = mockFirebase(t);
  records.set('domains/dominio-a/rules/regla-a', validRule());
  const first = await request('POST', '/regla-a/keys', { apiKey: 'del-cliente', keyHash: 'no-usar' });
  assert.equal(first.status, 201);
  assert.equal(first.headers.get('Cache-Control'), 'no-store');
  const key = await first.json();
  assert.equal(typeof key.apiKey, 'string');
  assert.notEqual(key.apiKey, 'del-cliente');
  const hash = createHash('sha256').update(key.apiKey).digest('hex');
  assert.deepEqual(records.get(`domains/dominio-a/rules/regla-a/keys/${key.id}`), { keyHash: hash });
  const second = await request('POST', '/regla-a/keys');
  const other = await second.json();
  assert.notEqual(key.apiKey, other.apiKey);
  const listed = await request('GET', '/regla-a/keys');
  assert.deepEqual(await listed.json(), [{ id: key.id }, { id: other.id }]);
  const config = await cacheConfig();
  const data = await config.json();
  assert.ok(data.domains[0].rules[0].apiKeys.includes(hash));
  assert.equal(JSON.stringify(data).includes(key.apiKey), false);
});

test('eliminar una clave la quita del listado y de la configuración para la caché', async (t) => {
  const records = mockFirebase(t);
  records.set('domains/dominio-a/rules/regla-a', validRule());
  const created = await request('POST', '/regla-a/keys');
  const key = await created.json();
  const removed = await request('DELETE', `/regla-a/keys/${key.id}`);
  assert.equal(removed.status, 204);
  assert.equal(records.has(`domains/dominio-a/rules/regla-a/keys/${key.id}`), false);
  const listed = await request('GET', '/regla-a/keys');
  assert.deepEqual(await listed.json(), []);
  const config = await cacheConfig();
  assert.deepEqual((await config.json()).domains[0].rules[0].apiKeys, []);
});

test('las claves no permiten saltar a otra regla ni usar rutas de documento inválidas', async (t) => {
  const records = mockFirebase(t);
  records.set('domains/dominio-a/rules/regla-a', validRule());
  records.set('domains/dominio-a/rules/regla-b/keys/clave-b', { keyHash: 'hash' });
  const original = [...records];
  for (const response of [
    await request('GET', '/inexistente/keys'),
    await request('POST', '/inexistente/keys'),
    await request('DELETE', '/regla-a/keys/clave-b'),
    await request('GET', '/ruta%2Fno%2Fvalida/keys'),
    await request('DELETE', '/regla-a/keys/ruta%2Fno%2Fvalida'),
  ]) assert.equal(response.status, 404);
  assert.deepEqual([...records], original);
});

test('crear claves requiere que el mecanismo guardado de la regla sea API key', async (t) => {
  const records = mockFirebase(t);
  records.set('domains/dominio-a/rules/regla-a', validRule({ authMode: 'password' }));
  const response = await request('POST', '/regla-a/keys');
  assert.equal(response.status, 409);
  assert.equal(records.size, 3);
});

test('un dominio pendiente puede configurar claves sin enviarlas aún a la caché', async (t) => {
  const records = mockFirebase(t);
  records.get('domains/dominio-a').verified = false;
  records.set('domains/dominio-a/rules/regla-a', validRule());
  const created = await request('POST', '/regla-a/keys');
  assert.equal(created.status, 201);
  const config = await cacheConfig();
  assert.deepEqual(await config.json(), { domains: [] });
});

test('un fallo al leer, crear o eliminar claves no revela datos ni devuelve éxito', async (t) => {
  for (const [failAt, method, path] of [['keys-read', 'GET', '/regla-a/keys'], ['key-create', 'POST', '/regla-a/keys'], ['key-delete', 'DELETE', '/regla-a/keys/clave-a']]) {
    const records = mockFirebase(t, failAt);
    records.set('domains/dominio-a/rules/regla-a', validRule());
    records.set('domains/dominio-a/rules/regla-a/keys/clave-a', { keyHash: 'hash-existente' });
    const original = [...records];
    const response = await request(method, path);
    assert.equal(response.status, 503);
    assert.deepEqual(Object.keys(await response.json()), ['error']);
    assert.deepEqual([...records], original);
  }
});
