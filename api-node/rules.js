import express from 'express';
import { createHash, randomBytes, scrypt } from 'node:crypto';
import { promisify } from 'node:util';
import { requireAuth } from './auth.js';
import { getDb } from './firestore.js';

const router = express.Router({ mergeParams: true });
const policies = ['LRU', 'LFU', 'FIFO', 'MRU', 'Random'];
const derivePassword = promisify(scrypt);

router.use(requireAuth, async (req, res, next) => {
  if (req.params.domainId.includes('/')) {
    return res.status(404).json({ error: 'No se encontró este dominio en tu cuenta.' });
  }
  try {
    req.domainRef = getDb().collection('domains').doc(req.params.domainId);
    const snapshot = await req.domainRef.get();
    req.domainData = snapshot.data();
    if (!snapshot.exists || req.domainData.ownerId !== req.user.uid) {
      return res.status(404).json({ error: 'No se encontró este dominio en tu cuenta.' });
    }
    next();
  } catch {
    res.status(503).json({ error: 'No se pudo consultar el dominio. Intenta de nuevo.' });
  }
});

function ruleData(body, domainLimit) {
  const pathPattern = typeof body?.pathPattern === 'string' ? body.pathPattern.trim() : '';
  if (!pathPattern.startsWith('/') || /\s/.test(pathPattern)) {
    return { error: 'Escribe una ruta que empiece con / y no tenga espacios.' };
  }
  if (!Number.isSafeInteger(body.maxCacheBytes) || body.maxCacheBytes <= 0 || body.maxCacheBytes > domainLimit) {
    return { error: 'El tamaño de la regla debe ser un entero positivo de bytes y no superar el límite del dominio.' };
  }
  // "none" deja la ruta pública: la caché la guarda sin pedir credenciales.
  if (!['none', 'api-key', 'password'].includes(body.authMode)) {
    return { error: 'Selecciona sin autenticación, API key o usuario y contraseña.' };
  }
  if (!Array.isArray(body.fileTypes) || body.fileTypes.length === 0) {
    return { error: 'Agrega al menos un tipo de archivo.' };
  }
  const extensions = new Set();
  const fileTypes = [];
  for (const type of body.fileTypes) {
    const extension = typeof type?.extension === 'string' ? type.extension.trim().replace(/^\./, '').toLowerCase() : '';
    // "*" aplica a cualquier extensión que no tenga un tipo propio en la regla.
    if ((extension !== '*' && !/^[a-z0-9]+(?:[.-][a-z0-9]+)*$/.test(extension)) || extensions.has(extension)) {
      return { error: 'Los tipos de archivo deben tener extensiones válidas y no repetirse.' };
    }
    if (!Number.isSafeInteger(type.ttlSeconds) || type.ttlSeconds < 0 || !policies.includes(type.replacementPolicy)) {
      return { error: 'Cada tipo necesita un TTL entero mayor o igual que 0 y una política válida.' };
    }
    extensions.add(extension);
    fileTypes.push({ extension, ttlSeconds: type.ttlSeconds, replacementPolicy: type.replacementPolicy });
  }
  return { pathPattern, maxCacheBytes: body.maxCacheBytes, authMode: body.authMode, fileTypes };
}

function ruleResponse(id, { pathPattern, maxCacheBytes, authMode, fileTypes }) {
  return { id, pathPattern, maxCacheBytes, authMode, fileTypes };
}

router.get('/', async (req, res) => {
  try {
    const snapshot = await req.domainRef.collection('rules').get();
    res.json(snapshot.docs.map((doc) => ruleResponse(doc.id, doc.data())));
  } catch {
    res.status(503).json({ error: 'No se pudieron cargar las reglas. Intenta de nuevo.' });
  }
});

async function saveRule(req, res) {
  if (req.params.ruleId?.includes('/')) return res.status(404).json({ error: 'No se encontró esta regla en el dominio.' });
  const rule = ruleData(req.body, req.domainData.maxCacheBytes);
  if (rule.error) return res.status(400).json(rule);
  try {
    const rules = req.domainRef.collection('rules');
    const ref = req.params.ruleId ? rules.doc(req.params.ruleId) : rules.doc();
    if (req.method === 'PUT') {
      if (!(await ref.get()).exists) return res.status(404).json({ error: 'No se encontró esta regla en el dominio.' });
      await ref.update(rule);
    } else {
      await ref.create(rule);
    }
    res.status(req.method === 'POST' ? 201 : 200).json(ruleResponse(ref.id, rule));
  } catch {
    res.status(503).json({ error: 'No se pudo guardar la regla. Intenta de nuevo.' });
  }
}

router.post('/', express.json(), saveRule);
router.put('/:ruleId', express.json(), saveRule);

router.delete('/:ruleId', async (req, res) => {
  if (req.params.ruleId.includes('/')) return res.status(404).json({ error: 'No se encontró esta regla en el dominio.' });
  try {
    const ref = req.domainRef.collection('rules').doc(req.params.ruleId);
    if (!(await ref.get()).exists) return res.status(404).json({ error: 'No se encontró esta regla en el dominio.' });
    await getDb().recursiveDelete(ref);
    res.status(204).end();
  } catch {
    res.status(503).json({ error: 'No se pudo eliminar la regla. Intenta de nuevo.' });
  }
});

router.use(['/:ruleId/keys', '/:ruleId/users'], async (req, res, next) => {
  res.set('Cache-Control', 'no-store');
  if (req.params.ruleId.includes('/')) return res.status(404).json({ error: 'No se encontró esta regla en el dominio.' });
  try {
    req.ruleRef = req.domainRef.collection('rules').doc(req.params.ruleId);
    const snapshot = await req.ruleRef.get();
    if (!snapshot.exists) return res.status(404).json({ error: 'No se encontró esta regla en el dominio.' });
    req.ruleData = snapshot.data();
    next();
  } catch {
    res.status(503).json({ error: 'No se pudo consultar la regla. Intenta de nuevo.' });
  }
});

router.get('/:ruleId/keys', async (req, res) => {
  try {
    const keys = await req.ruleRef.collection('keys').get();
    res.json(keys.docs.map((key) => ({ id: key.id })));
  } catch {
    res.status(503).json({ error: 'No se pudieron cargar las claves. Intenta de nuevo.' });
  }
});

router.post('/:ruleId/keys', async (req, res) => {
  if (req.ruleData.authMode !== 'api-key') {
    return res.status(409).json({ error: 'Guarda la regla con acceso por API key antes de crear claves.' });
  }
  try {
    const apiKey = randomBytes(24).toString('base64url');
    const keyHash = createHash('sha256').update(apiKey).digest('hex');
    const ref = req.ruleRef.collection('keys').doc();
    await ref.create({ keyHash });
    res.status(201).json({ id: ref.id, apiKey });
  } catch {
    res.status(503).json({ error: 'No se pudo crear la clave. Intenta de nuevo.' });
  }
});

router.delete('/:ruleId/keys/:keyId', async (req, res) => {
  if (req.params.keyId.includes('/')) return res.status(404).json({ error: 'No se encontró esta clave en la regla.' });
  try {
    const ref = req.ruleRef.collection('keys').doc(req.params.keyId);
    if (!(await ref.get()).exists) return res.status(404).json({ error: 'No se encontró esta clave en la regla.' });
    await ref.delete();
    res.status(204).end();
  } catch {
    res.status(503).json({ error: 'No se pudo eliminar la clave. Intenta de nuevo.' });
  }
});

router.get('/:ruleId/users', async (req, res) => {
  try {
    const users = await req.ruleRef.collection('users').get();
    res.json(users.docs.map((user) => ({ id: user.id, username: user.data().username })));
  } catch {
    res.status(503).json({ error: 'No se pudieron cargar los usuarios. Intenta de nuevo.' });
  }
});

async function saveUser(req, res) {
  if (req.params.userId?.includes('/')) return res.status(404).json({ error: 'No se encontró este usuario en la regla.' });
  if (req.ruleData.authMode !== 'password') {
    return res.status(409).json({ error: 'Guarda la regla con acceso por usuario y contraseña antes de administrar usuarios.' });
  }
  const username = typeof req.body?.username === 'string' ? req.body.username.trim() : '';
  const password = req.body?.password;
  if (!username) return res.status(400).json({ error: 'Escribe un nombre de usuario.' });
  if ((password !== undefined && typeof password !== 'string') || (req.method === 'POST' && !password)) {
    return res.status(400).json({ error: 'Escribe una contraseña para crear el usuario; al editar puedes dejarla vacía.' });
  }
  try {
    const users = req.ruleRef.collection('users');
    const ref = req.params.userId ? users.doc(req.params.userId) : users.doc();
    if (req.method === 'PUT' && !(await ref.get()).exists) {
      return res.status(404).json({ error: 'No se encontró este usuario en la regla.' });
    }
    const matching = await users.where('username', '==', username).get();
    if (matching.docs.some((user) => user.id !== ref.id)) {
      return res.status(409).json({ error: 'Este nombre de usuario ya existe en la regla.' });
    }
    const data = { username };
    if (password) {
      const salt = randomBytes(16);
      const hash = await derivePassword(password, salt, 64);
      data.passwordHash = `scrypt$${salt.toString('hex')}$${hash.toString('hex')}`;
    }
    if (req.method === 'POST') await ref.create(data);
    else await ref.update(data);
    res.status(req.method === 'POST' ? 201 : 200).json({ id: ref.id, username });
  } catch {
    res.status(503).json({ error: 'No se pudo guardar el usuario. Intenta de nuevo.' });
  }
}

router.post('/:ruleId/users', express.json(), saveUser);
router.put('/:ruleId/users/:userId', express.json(), saveUser);

router.delete('/:ruleId/users/:userId', async (req, res) => {
  if (req.params.userId.includes('/')) return res.status(404).json({ error: 'No se encontró este usuario en la regla.' });
  try {
    const ref = req.ruleRef.collection('users').doc(req.params.userId);
    if (!(await ref.get()).exists) return res.status(404).json({ error: 'No se encontró este usuario en la regla.' });
    await ref.delete();
    res.status(204).end();
  } catch {
    res.status(503).json({ error: 'No se pudo eliminar el usuario. Intenta de nuevo.' });
  }
});

export default router;
