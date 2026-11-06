import assert from 'node:assert/strict';
import { createHash, generateKeyPairSync } from 'node:crypto';
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
const cacheToken = 'token-local-para-pruebas';
let server;
let baseUrl;

before(async () => {
  server = app.listen(0, '127.0.0.1');
  await new Promise((resolve) => server.once('listening', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});
after(() => new Promise((resolve) => server.close(resolve)));
beforeEach(() => { process.env.CACHE_API_TOKEN = cacheToken; });

// Solo simula las colecciones que puede leer este endpoint; no conecta con Firebase.
function mockFirestore(t, domains = [], failAt) {
  const requested = [];
  const collection = t.mock.method(db, 'collection', (name) => {
    assert.equal(name, 'domains');
    return {
      where(field, operator, value) {
        assert.deepEqual([field, operator, value], ['verified', '==', true]);
        return {
          async get() {
            if (failAt === 'domains') throw new Error('Detalle privado de Firestore');
            return {
              docs: domains.filter((domain) => domain.verified === true).map((domain) => ({
                data: () => domain,
                ref: {
                  collection(name) {
                    assert.equal(name, 'rules');
                    requested.push(`${domain.name}/rules`);
                    return {
                      async get() {
                        if (failAt === 'rules') throw new Error('Detalle privado de Firestore');
                        return {
                          docs: (domain.rules ?? []).map((rule) => ({
                            id: rule.id, data: () => rule,
                            ref: {
                              collection(name) {
                                assert.equal(name, 'keys');
                                requested.push(`${domain.name}/${rule.id}/keys`);
                                return {
                                  async get() {
                                    if (failAt === 'keys') throw new Error('Detalle privado de Firestore');
                                    return { docs: (rule.keys ?? []).map((key) => ({ data: () => key })) };
                                  },
                                };
                              },
                            },
                          })),
                        };
                      },
                    };
                  },
                },
              })),
            };
          },
        };
      },
    };
  });
  return { requested, collection };
}

function readConfig(authorization = `Bearer ${cacheToken}`) {
  return fetch(`${baseUrl}/api/cache/config`, { headers: authorization ? { Authorization: authorization } : {} });
}

function verifiedDomain() {
  return {
    ownerId: 'cuenta-a', name: 'apache.p2.local', verified: true,
    originUrl: 'http://apache-apache', maxCacheBytes: 1048576,
    rules: [{ id: 'regla-a', pathPattern: '/*', maxCacheBytes: 524288, authMode: 'api-key', fileTypes: [], keys: [] }],
  };
}

test('rechaza el acceso sin el token de caché antes de consultar Firestore', async (t) => {
  const { collection } = mockFirestore(t);
  for (const authorization of [null, 'Basic incorrecto', 'Bearer otro-token', 'Bearer TOKEN-LOCAL-PARA-PRUEBAS', 'Bearer token-local-para-pruebaS']) {
    const response = await readConfig(authorization);
    assert.equal(response.status, 401);
    assert.equal(response.headers.get('Cache-Control'), 'no-store');
  }
  assert.equal(collection.mock.callCount(), 0);
});

test('sin CACHE_API_TOKEN no permite acceso ni consulta Firestore', async (t) => {
  const { collection } = mockFirestore(t);
  delete process.env.CACHE_API_TOKEN;
  const response = await readConfig();
  assert.equal(response.status, 503);
  assert.equal(collection.mock.callCount(), 0);
});

test('entrega solo dominios verificados y los campos necesarios para la caché', async (t) => {
  const keyHash = createHash('sha256').update('clave-de-prueba').digest('hex');
  const fileType = { extension: 'html', ttlSeconds: 60, replacementPolicy: 'LRU' };
  const domain = {
    ...verifiedDomain(), verificationToken: 'token-txt-privado',
    rules: [
      {
        id: 'regla-a', pathPattern: '/*', maxCacheBytes: 524288, authMode: 'api-key',
        replacementPolicy: 'LRU', fileTypes: [{ ...fileType, datoPrivado: 'no-enviar' }],
        keys: [{ keyHash, apiKey: 'no-enviar-clave-original' }],
      },
      {
        id: 'regla-b', pathPattern: '/privado/*', maxCacheBytes: 262144, authMode: 'password',
        fileTypes: [], passwordHash: 'no-enviar-hash', users: [{ username: 'privado', passwordHash: 'no-enviar-hash' }],
      },
    ],
  };
  const { requested } = mockFirestore(t, [domain, { ...verifiedDomain(), name: 'pendiente.local', verified: false }]);
  const response = await readConfig();
  assert.equal(response.status, 200);
  assert.equal(response.headers.get('Cache-Control'), 'no-store');
  assert.deepEqual(await response.json(), {
    domains: [{
      name: domain.name, originUrl: domain.originUrl, maxCacheBytes: domain.maxCacheBytes,
      rules: [
        { id: 'regla-a', pathPattern: '/*', maxCacheBytes: 524288, authMode: 'api-key', replacementPolicy: 'LRU', fileTypes: [fileType], apiKeys: [keyHash] },
        { id: 'regla-b', pathPattern: '/privado/*', maxCacheBytes: 262144, authMode: 'password', fileTypes: [], apiKeys: [] },
      ],
    }],
  });
  assert.deepEqual(requested, ['apache.p2.local/rules', 'apache.p2.local/regla-a/keys']);
});

test('sin dominios verificados entrega una configuración vacía', async (t) => {
  mockFirestore(t, [{ ...verifiedDomain(), verified: false }]);
  const response = await readConfig();
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { domains: [] });
});

test('si falla una lectura no entrega una configuración incompleta', async (t) => {
  for (const failAt of ['domains', 'rules', 'keys']) {
    mockFirestore(t, [verifiedDomain()], failAt);
    const response = await readConfig();
    assert.equal(response.status, 503);
    assert.doesNotMatch(JSON.stringify(await response.json()), /Detalle privado/);
  }
});

test('una nueva consulta refleja las claves eliminadas y los dominios desactivados', async (t) => {
  const domain = verifiedDomain();
  domain.rules[0].keys.push({ keyHash: createHash('sha256').update('revocada').digest('hex') });
  mockFirestore(t, [domain]);
  const first = await readConfig();
  assert.equal((await first.json()).domains[0].rules[0].apiKeys.length, 1);
  domain.rules[0].keys.length = 0;
  const refreshed = await readConfig();
  assert.deepEqual((await refreshed.json()).domains[0].rules[0].apiKeys, []);
  domain.verified = false;
  const disabled = await readConfig();
  assert.deepEqual(await disabled.json(), { domains: [] });
});
