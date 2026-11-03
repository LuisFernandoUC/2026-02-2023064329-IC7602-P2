import express from 'express';
import { timingSafeEqual } from 'node:crypto';
import { getDb } from './firestore.js';

const router = express.Router();

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

export default router;
