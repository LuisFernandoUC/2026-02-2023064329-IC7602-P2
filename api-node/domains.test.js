import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';
import dns from 'node:dns/promises';
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

const auth = getAuth(getFirebaseApp());
const db = getDb();
let server;
let baseUrl;

before(async () => {
  server = app.listen(0, '127.0.0.1');
  await new Promise((resolve) => server.once('listening', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});
after(() => new Promise((resolve) => server.close(resolve)));

// Las cuentas y los documentos de estas pruebas solo existen en memoria.
function mockFirebase(t, records = new Map(), failAt) {
  t.mock.method(auth, 'verifyIdToken', async (token) => ({ uid: token }));
  function document(id) {
    return {
      id, path: `domains/${id}`,
      async get() {
        if (failAt === 'read') throw new Error('Detalle privado de Firestore');
        return { exists: records.has(id), data: () => records.get(id) };
      },
      async create(data) {
        if (records.has(id)) throw Object.assign(new Error('Duplicado'), { code: 6 });
        records.set(id, data);
      },
      async update(data) {
        if (failAt === 'update') throw new Error('Detalle privado de Firestore');
        assert.ok(records.has(id));
        records.set(id, { ...records.get(id), ...data });
      },
      collection(name) {
        return { get: async () => ({ docs: [...records].filter(([key]) => key.startsWith(`${id}/${name}/`) && !key.slice(`${id}/${name}/`.length).includes('/'))
          .map(([key, data]) => ({ id: key.split('/').at(-1), data: () => data, ref: document(key) })) }) };
      },
    };
  }
  t.mock.method(db, 'collection', (collection) => {
    assert.equal(collection, 'domains');
    return {
      where(field, operator, value) {
        assert.equal(operator, '==');
        return {
          async get() {
            return {
              docs: [...records].filter(([id, data]) => !id.includes('/') && data[field] === value)
                .map(([id, data]) => ({ id, data: () => data, ref: document(id) })),
            };
          },
        };
      },
      doc: document,
    };
  });
  t.mock.method(db, 'recursiveDelete', async (ref) => {
    if (failAt === 'delete') throw new Error('Detalle privado de Firestore');
    for (const key of records.keys()) if (key === ref.id || key.startsWith(`${ref.id}/`)) records.delete(key);
  });
  return records;
}

function createDomain(body, token = 'cuenta-a') {
  return fetch(`${baseUrl}/api/domains`, {
    method: 'POST',
    headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ originUrl: 'http://apache-apache', ...body }),
  });
}

test('todas las operaciones de dominios requieren sesión', async () => {
  for (const [method, path] of [['GET', ''], ['POST', ''], ['POST', '/dominio-a/verify'], ['DELETE', '/dominio-a']]) {
    const response = await fetch(`${baseUrl}/api/domains${path}`, { method });
    assert.equal(response.status, 401);
  }
});

test('crea un dominio pendiente para el propietario verificado', async (t) => {
  const records = mockFirebase(t);
  const response = await createDomain({
    name: ' APACHE.LOCAL ', maxCacheBytes: 1048576, originUrl: ' HTTP://APACHE-APACHE:80/ ',
    ownerId: 'otra-cuenta', verified: true, verificationToken: 'token-del-cliente',
  });
  assert.equal(response.status, 201);
  const domain = await response.json();
  assert.equal(domain.name, 'apache.local');
  assert.equal(domain.ownerId, 'cuenta-a');
  assert.equal(domain.verified, false);
  assert.equal(domain.maxCacheBytes, 1048576);
  assert.equal(domain.originUrl, 'http://apache-apache');
  assert.ok(domain.verificationToken);
  assert.notEqual(domain.verificationToken, 'token-del-cliente');
  assert.deepEqual(domain.verificationRecord, { name: '_verificacion.apache.local', value: domain.verificationToken });
  assert.equal(records.get(domain.id).ownerId, 'cuenta-a');
  assert.equal(records.get(domain.id).originUrl, domain.originUrl);
});

test('rechaza dominios y tamaños inválidos antes de guardar', async (t) => {
  const records = mockFirebase(t);
  for (const body of [
    { name: 'https://apache.local/ruta', maxCacheBytes: 1048576 },
    { name: 'apache.local', maxCacheBytes: 0 },
    { name: 'apache.local', maxCacheBytes: 1.5 },
    { name: 'apache.local', maxCacheBytes: Number.MAX_SAFE_INTEGER + 1 },
    { name: { domain: 'apache.local' }, maxCacheBytes: 1048576 },
  ]) {
    const response = await createDomain(body);
    assert.equal(response.status, 400);
  }
  assert.equal(records.size, 0);
});

test('un dominio duplicado no sobrescribe su configuración', async (t) => {
  const records = mockFirebase(t);
  const first = await createDomain({ name: 'apache.local', maxCacheBytes: 1048576 });
  assert.equal(first.status, 201);
  const original = await first.json();
  const duplicate = await createDomain({ name: 'APACHE.LOCAL', maxCacheBytes: 2097152, originUrl: 'http://otro-servidor' });
  assert.equal(duplicate.status, 409);
  assert.equal(records.size, 1);
  assert.equal(records.get(original.id).maxCacheBytes, 1048576);
  assert.equal(records.get(original.id).verificationToken, original.verificationToken);
  assert.equal(records.get(original.id).originUrl, original.originUrl);
});

test('rechaza un origen ausente o incompatible con la dirección base de la caché', async (t) => {
  const records = mockFirebase(t);
  for (const originUrl of [undefined, '', null, 42, 'apache-apache', 'ftp://servidor', 'javascript:alert(1)',
    'http://usuario:clave@servidor', 'http://servidor/archivos', 'http://servidor?dato=1', 'http://servidor#seccion']) {
    const response = await createDomain({ name: 'apache.local', maxCacheBytes: 1048576, originUrl });
    assert.equal(response.status, 400);
  }
  assert.equal(records.size, 0);
});

test('acepta orígenes HTTP y HTTPS con puerto y los conserva al listar', async (t) => {
  mockFirebase(t);
  for (const [name, originUrl] of [['apache.local', 'http://apache-apache:8080'], ['seguro.local', 'https://example.com:8443']]) {
    const response = await createDomain({ name, maxCacheBytes: 1048576, originUrl });
    assert.equal(response.status, 201);
    assert.equal((await response.json()).originUrl, originUrl);
  }
  const response = await fetch(`${baseUrl}/api/domains`, { headers: { Authorization: 'Bearer cuenta-a' } });
  assert.equal(response.status, 200);
  assert.deepEqual((await response.json()).map((domain) => domain.originUrl), ['http://apache-apache:8080', 'https://example.com:8443']);
});

test('el listado no permite consultar dominios de otra cuenta', async (t) => {
  mockFirebase(t, new Map([
    ['dominio-a', { ownerId: 'cuenta-a', name: 'apache.local', maxCacheBytes: 1048576, verified: false, verificationToken: 'token-a' }],
    ['dominio-b', { ownerId: 'cuenta-b', name: 'otro.local', maxCacheBytes: 2097152, verified: false, verificationToken: 'token-b' }],
  ]));
  const response = await fetch(`${baseUrl}/api/domains?ownerId=cuenta-b`, { headers: { Authorization: 'Bearer cuenta-a' } });
  assert.equal(response.status, 200);
  const domains = await response.json();
  assert.deepEqual(domains.map((domain) => domain.id), ['dominio-a']);
});

test('los fallos de Firestore no exponen detalles internos', async (t) => {
  mockFirebase(t);
  t.mock.method(db, 'collection', () => { throw new Error('Detalle privado de Firestore'); });
  const listed = await fetch(`${baseUrl}/api/domains`, { headers: { Authorization: 'Bearer cuenta-a' } });
  const created = await createDomain({ name: 'apache.local', maxCacheBytes: 1048576 });
  for (const response of [listed, created]) {
    assert.equal(response.status, 503);
    assert.doesNotMatch(JSON.stringify(await response.json()), /Detalle privado/);
  }
});

test('un cuerpo JSON incompleto devuelve un error JSON', async (t) => {
  const records = mockFirebase(t);
  const response = await fetch(`${baseUrl}/api/domains`, {
    method: 'POST', headers: { Authorization: 'Bearer cuenta-a', 'Content-Type': 'application/json' }, body: '{',
  });
  assert.equal(response.status, 400);
  assert.equal(typeof (await response.json()).error, 'string');
  assert.equal(records.size, 0);
});

function pendingDomain(changes = {}) {
  return { ownerId: 'cuenta-a', name: 'apache.local', originUrl: 'http://apache-apache', maxCacheBytes: 1048576, verified: false, verificationToken: 'token-guardado', ...changes };
}

function domainRequest(method, path, token = 'cuenta-a') {
  return fetch(`${baseUrl}/api/domains/${path}`, { method, headers: { Authorization: `Bearer ${token}` } });
}

test('verifica el TXT guardado, une sus fragmentos y entrega el dominio a la caché', async (t) => {
  const domain = pendingDomain();
  const records = mockFirebase(t, new Map([['dominio-a', domain]]));
  const lookup = t.mock.method(dns, 'resolveTxt', async (name) => {
    assert.equal(name, '_verificacion.apache.local');
    return [['otro-registro'], ['token-', 'guardado']];
  });
  const response = await domainRequest('POST', 'dominio-a/verify');
  assert.equal(response.status, 200);
  assert.deepEqual(records.get('dominio-a'), { ...domain, verified: true });
  assert.equal((await response.json()).verified, true);
  assert.equal(lookup.mock.callCount(), 1);
  process.env.CACHE_API_TOKEN = 'token-cache-pruebas';
  const config = await fetch(`${baseUrl}/api/cache/config`, { headers: { Authorization: 'Bearer token-cache-pruebas' } });
  assert.equal(config.status, 200);
  assert.deepEqual((await config.json()).domains, [{ name: domain.name, originUrl: domain.originUrl, maxCacheBytes: domain.maxCacheBytes, rules: [] }]);
});

test('un TXT ausente o distinto no verifica el dominio', async (t) => {
  const records = mockFirebase(t, new Map([['dominio-a', pendingDomain()]]));
  let txt = [];
  t.mock.method(dns, 'resolveTxt', async () => txt);
  for (const value of [[], [['incorrecto']], [['token-'], ['guardado']]]) {
    txt = value;
    assert.equal((await domainRequest('POST', 'dominio-a/verify')).status, 409);
    assert.equal(records.get('dominio-a').verified, false);
  }
});

test('distingue un TXT no publicado de un fallo del servicio DNS', async (t) => {
  const records = mockFirebase(t, new Map([['dominio-a', pendingDomain()]]));
  let code;
  t.mock.method(dns, 'resolveTxt', async () => { throw Object.assign(new Error('Detalle privado DNS'), { code }); });
  for (const [value, status] of [['ENODATA', 409], ['ENOTFOUND', 409], ['ESERVFAIL', 503], ['ETIMEOUT', 503]]) {
    code = value;
    const response = await domainRequest('POST', 'dominio-a/verify');
    assert.equal(response.status, status);
    assert.doesNotMatch(JSON.stringify(await response.json()), /Detalle privado/);
    assert.equal(records.get('dominio-a').verified, false);
  }
});

test('repetir la verificación no cambia el dominio ni consulta el DNS de nuevo', async (t) => {
  const domain = pendingDomain({ verified: true });
  const records = mockFirebase(t, new Map([['dominio-a', domain]]));
  const lookup = t.mock.method(dns, 'resolveTxt', async () => { throw new Error('No debe consultar'); });
  const response = await domainRequest('POST', 'dominio-a/verify');
  assert.equal(response.status, 200);
  assert.deepEqual(records.get('dominio-a'), domain);
  assert.equal(lookup.mock.callCount(), 0);
});

test('no verifica un nombre que ya está verificado en otra cuenta', async (t) => {
  const records = mockFirebase(t, new Map([
    ['dominio-a', pendingDomain()],
    ['dominio-b', pendingDomain({ ownerId: 'cuenta-b', verified: true })],
  ]));
  t.mock.method(dns, 'resolveTxt', async () => [['token-guardado']]);
  assert.equal((await domainRequest('POST', 'dominio-a/verify')).status, 409);
  assert.equal(records.get('dominio-a').verified, false);
});

test('verificar y eliminar comprueban el propietario antes de consultar DNS o borrar datos', async (t) => {
  const records = mockFirebase(t, new Map([['dominio-a', pendingDomain()]]));
  const lookup = t.mock.method(dns, 'resolveTxt', async () => []);
  const original = [...records];
  for (const [id, token] of [['dominio-a', 'cuenta-b'], ['inexistente', 'cuenta-a'], ['ruta%2Fno%2Fvalida', 'cuenta-a']]) {
    assert.equal((await domainRequest('POST', `${id}/verify`, token)).status, 404);
    assert.equal((await domainRequest('DELETE', id, token)).status, 404);
  }
  assert.deepEqual([...records], original);
  assert.equal(lookup.mock.callCount(), 0);
});

test('eliminar borra el dominio y sus subcolecciones, y lo quita de la configuración', async (t) => {
  const records = mockFirebase(t, new Map([
    ['dominio-a', pendingDomain({ verified: true })],
    ['dominio-a/rules/regla-a', { pathPattern: '/*' }],
    ['dominio-a/rules/regla-a/keys/clave-a', { keyHash: 'hash' }],
    ['dominio-a/rules/regla-a/users/usuario-a', { username: 'lector', passwordHash: 'hash' }],
    ['dominio-b', pendingDomain({ name: 'otro.local', ownerId: 'cuenta-b', verified: true })],
  ]));
  const response = await domainRequest('DELETE', 'dominio-a');
  assert.equal(response.status, 204);
  assert.deepEqual([...records.keys()], ['dominio-b']);
  process.env.CACHE_API_TOKEN = 'token-cache-pruebas';
  const config = await fetch(`${baseUrl}/api/cache/config`, { headers: { Authorization: 'Bearer token-cache-pruebas' } });
  assert.deepEqual((await config.json()).domains.map((domain) => domain.name), ['otro.local']);
});

test('los fallos de Firestore no confirman verificaciones o eliminaciones fallidas', async (t) => {
  t.mock.method(dns, 'resolveTxt', async () => [['token-guardado']]);
  for (const [failAt, method, path] of [['read', 'POST', 'dominio-a/verify'], ['update', 'POST', 'dominio-a/verify'], ['delete', 'DELETE', 'dominio-a']]) {
    const records = mockFirebase(t, new Map([['dominio-a', pendingDomain()]]), failAt);
    const original = [...records];
    const response = await domainRequest(method, path);
    assert.equal(response.status, 503);
    assert.doesNotMatch(JSON.stringify(await response.json()), /Detalle privado/);
    assert.deepEqual([...records], original);
  }
});
