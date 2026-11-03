import express from 'express';
import { requireAuth } from './auth.js';
import { getDb } from './firestore.js';

const router = express.Router({ mergeParams: true });
const policies = ['LRU', 'LFU', 'FIFO', 'MRU', 'Random'];

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
  if (!['api-key', 'password'].includes(body.authMode)) {
    return { error: 'Selecciona API key o usuario y contraseña.' };
  }
  if (!Array.isArray(body.fileTypes) || body.fileTypes.length === 0) {
    return { error: 'Agrega al menos un tipo de archivo.' };
  }
  const extensions = new Set();
  const fileTypes = [];
  for (const type of body.fileTypes) {
    const extension = typeof type?.extension === 'string' ? type.extension.trim().replace(/^\./, '').toLowerCase() : '';
    if (!/^[a-z0-9]+(?:[.-][a-z0-9]+)*$/.test(extension) || extensions.has(extension)) {
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

export default router;
