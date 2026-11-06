import express from 'express';
import { createHmac, scrypt, timingSafeEqual } from 'node:crypto';
import { promisify } from 'node:util';
import { getDb } from './firestore.js';

const router = express.Router();
const derivePassword = promisify(scrypt);

function requireCache(req, res, next) {
  res.set('Cache-Control', 'no-store');
  const expected = process.env.CACHE_API_TOKEN;
  if (!expected) {
    return res.status(503).json({ error: 'El acceso de las cachés no está configurado.' });
  }

  const token = req.get('Authorization')?.match(/^Bearer (\S+)$/i)?.[1];
  const received = Buffer.from(token ?? '');
  const configured = Buffer.from(expected);
  if (received.length !== configured.length || !timingSafeEqual(received, configured)) {
    return res.status(401).json({ error: 'Caché no autorizada.' });
  }
  next();
}

router.get('/config', requireCache, async (req, res) => {
  try {
    const snapshot = await getDb().collection('domains').where('verified', '==', true).get();
    const domains = await Promise.all(snapshot.docs.map(async (doc) => {
      const { name, originUrl, maxCacheBytes } = doc.data();
      const rules = await doc.ref.collection('rules').get();
      return {
        name, originUrl, maxCacheBytes,
        rules: await Promise.all(rules.docs.map(async (rule) => {
          const { pathPattern, maxCacheBytes, authMode, replacementPolicy, fileTypes } = rule.data();
          const apiKeys = authMode === 'api-key'
            ? (await rule.ref.collection('keys').get()).docs.map((key) => key.data().keyHash)
            : [];
          return {
            id: rule.id, pathPattern, maxCacheBytes, authMode, replacementPolicy,
            fileTypes: fileTypes.map(({ extension, ttlSeconds, replacementPolicy }) => ({ extension, ttlSeconds, replacementPolicy })),
            apiKeys,
          };
        })),
      };
    }));
    res.json({ domains });
  } catch {
    res.status(503).json({ error: 'No se pudo cargar la configuración de las cachés. Intenta de nuevo.' });
  }
});

function matches(pattern, path) {
  const expression = pattern.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*').replace(/\?/g, '.');
  return new RegExp(`^${expression}$`).test(path);
}

async function checkPassword(password, stored) {
  if (typeof stored !== 'string' || !/^scrypt\$[a-f0-9]{32}\$[a-f0-9]{128}$/.test(stored)) return false;
  const [, salt, hash] = stored.split('$');
  const received = await derivePassword(password, Buffer.from(salt, 'hex'), 64);
  return timingSafeEqual(received, Buffer.from(hash, 'hex'));
}

router.post('/sessions', express.json(), async (req, res) => {
  res.set('Cache-Control', 'no-store');
  const { domain: inputDomain, path, username: inputUsername, password, callback } = req.body ?? {};
  if (![inputDomain, path, inputUsername, password, callback].every((value) => typeof value === 'string' && value)) {
    return res.status(400).json({ error: 'Faltan datos del recurso o las credenciales.' });
  }
  const domain = inputDomain.trim().toLowerCase();
  const username = inputUsername.trim();
  if (!domain || !username || !path.startsWith('/') || path.startsWith('//') || /[?#\s\\]/.test(path)) {
    return res.status(400).json({ error: 'Los datos del recurso o el usuario no son válidos.' });
  }
  let back;
  try {
    back = new URL(callback);
    const next = back.searchParams.get('next') ?? path;
    if (!['http:', 'https:'].includes(back.protocol) || back.hostname !== domain || back.pathname !== '/__zc/auth'
      || back.username || back.password || back.hash || !next.startsWith('/') || next.startsWith('//')
      || /[\s\\#]/.test(next) || next.split('?')[0] !== path) throw new Error();
    back.searchParams.set('next', next);
  } catch {
    return res.status(400).json({ error: 'La dirección de regreso al recurso no es válida.' });
  }
  const secret = process.env.CACHE_SESSION_SECRET;
  const ttl = Number(process.env.CACHE_SESSION_SECONDS);
  if (!secret || Buffer.byteLength(secret) < 32 || !Number.isSafeInteger(ttl) || ttl <= 0) {
    return res.status(503).json({ error: 'El acceso a recursos no está configurado.' });
  }
  const denied = () => res.status(401).json({ error: 'Usuario o contraseña incorrectos.' });
  try {
    const domains = await getDb().collection('domains').where('name', '==', domain).get();
    const verified = domains.docs.filter((doc) => doc.data().verified === true);
    if (verified.length !== 1) return denied();
    const rules = await verified[0].ref.collection('rules').get();
    const specificity = (rule) => rule.data().pathPattern.replace(/[*?]/g, '').length;
    const rule = rules.docs.filter((rule) => matches(rule.data().pathPattern, path))
      .sort((a, b) => specificity(b) - specificity(a))[0];
    if (!rule || rule.data().authMode !== 'password') return denied();
    const users = await rule.ref.collection('users').where('username', '==', username).get();
    if (users.docs.length !== 1 || !await checkPassword(password, users.docs[0].data().passwordHash)) return denied();

    const exp = Math.floor(Date.now() / 1000) + ttl;
    if (!Number.isSafeInteger(exp)) throw new Error();
    const encode = (value) => Buffer.from(JSON.stringify(value)).toString('base64url');
    const body = `${encode({ alg: 'HS256', typ: 'JWT' })}.${encode({ domain, sub: username, rule: rule.id, exp })}`;
    const signature = createHmac('sha256', secret).update(body).digest('base64url');
    back.searchParams.set('token', `${body}.${signature}`);
    res.status(201).json({ redirect: back.toString(), expiresAt: exp });
  } catch {
    res.status(503).json({ error: 'No se pudo iniciar la sesión del recurso. Intenta de nuevo.' });
  }
});

export default router;
