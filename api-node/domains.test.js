import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';
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
function mockFirebase(t, records = new Map()) {
  t.mock.method(auth, 'verifyIdToken', async (token) => ({ uid: token }));
  t.mock.method(db, 'collection', (collection) => {
    assert.equal(collection, 'domains');
    return {
      where(field, operator, ownerId) {
        assert.equal(field, 'ownerId');
        assert.equal(operator, '==');
        return {
          async get() {
            return {
              docs: [...records].filter(([, data]) => data.ownerId === ownerId)
                .map(([id, data]) => ({ id, data: () => data })),
            };
          },
        };
      },
      doc(id) {
        return {
          id,
          async create(data) {
            if (records.has(id)) throw Object.assign(new Error('Duplicado'), { code: 6 });
            records.set(id, data);
          },
        };
      },
    };
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

test('listar y crear dominios requieren sesión', async () => {
  for (const method of ['GET', 'POST']) {
    const response = await fetch(`${baseUrl}/api/domains`, { method });
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
