import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';
import { after, before, test } from 'node:test';
import express from 'express';
import { getAuth } from 'firebase-admin/auth';
import app from './app.js';
import { requireAuth } from './auth.js';
import { getFirebaseApp } from './firebase.js';

// Credenciales locales de prueba. No se conectan con un proyecto real.
process.env.FIREBASE_PROJECT_ID = 'demo-proxy-cache';
process.env.FIREBASE_CLIENT_EMAIL = 'test@demo-proxy-cache.iam.gserviceaccount.com';
process.env.FIREBASE_PRIVATE_KEY = generateKeyPairSync('rsa', {
  modulusLength: 2048,
  privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
  publicKeyEncoding: { type: 'spki', format: 'pem' },
}).privateKey;

const auth = getAuth(getFirebaseApp());
const testApp = express();

// Esta ruta solo existe durante las pruebas del middleware.
testApp.get('/protegida', requireAuth, (req, res) => {
  res.json({ id: req.user.uid, email: req.user.email ?? null });
});
testApp.use(app);

let server;
let baseUrl;

before(async () => {
  server = testApp.listen(0, '127.0.0.1');
  await new Promise((resolve) => server.once('listening', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(() => new Promise((resolve) => server.close(resolve)));

test('health sigue disponible sin sesión', async () => {
  const response = await fetch(`${baseUrl}/api/health`);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { status: 'ok' });
});

test('rechaza solicitudes sin Bearer y tokens inválidos', async () => {
  for (const authorization of ['', 'Basic usuario', 'Bearer', 'Bearer token-invalido']) {
    const response = await fetch(`${baseUrl}/protegida`, {
      headers: authorization ? { Authorization: authorization } : {},
    });
    assert.equal(response.status, 401);
    assert.equal(typeof (await response.json()).error, 'string');
  }
});

test('devuelve la identidad verificada, sin aceptar un id del cliente', async (t) => {
  t.mock.method(auth, 'verifyIdToken', async (token) => {
    assert.equal(token, 'token-de-prueba');
    return { uid: 'cuenta-verificada', email: 'cuenta@example.com' };
  });

  const response = await fetch(`${baseUrl}/protegida?id=otra-cuenta`, {
    headers: { Authorization: 'Bearer token-de-prueba' },
  });
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { id: 'cuenta-verificada', email: 'cuenta@example.com' });
});

test('rechaza una sesión vencida', async (t) => {
  t.mock.method(auth, 'verifyIdToken', async () => {
    throw Object.assign(new Error('Token vencido'), { code: 'auth/id-token-expired' });
  });

  const response = await fetch(`${baseUrl}/protegida`, {
    headers: { Authorization: 'Bearer token-vencido' },
  });
  assert.equal(response.status, 401);
});

test('un fallo del servicio no permite acceder ni expone detalles', async (t) => {
  t.mock.method(auth, 'verifyIdToken', async () => {
    throw new Error('Detalle interno que no debe enviarse');
  });

  const response = await fetch(`${baseUrl}/protegida`, {
    headers: { Authorization: 'Bearer token-de-prueba' },
  });
  assert.equal(response.status, 503);
  assert.deepEqual(await response.json(), { error: 'No se pudo validar la sesión. Intenta de nuevo.' });
});
