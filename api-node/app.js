import express from 'express';
import cors from 'cors';
import { randomUUID } from 'node:crypto';
import dns from 'node:dns/promises';
import { requireAuth } from './auth.js';
import { getDb } from './firestore.js';
import cacheRouter from './cache.js';
import rulesRouter from './rules.js';

const app = express();

app.use(cors({ origin: process.env.UI_ORIGIN || false }));

app.get('/api/health', (req, res) => {
  res.json({ status: 'ok' });
});

function domainResponse(id, domain) {
  return {
    id,
    ...domain,
    verificationRecord: { name: `_verificacion.${domain.name}`, value: domain.verificationToken },
  };
}

app.get('/api/domains', requireAuth, async (req, res) => {
  try {
    const snapshot = await getDb().collection('domains').where('ownerId', '==', req.user.uid).get();
    res.json(snapshot.docs.map((doc) => domainResponse(doc.id, doc.data())));
  } catch {
    res.status(503).json({ error: 'No se pudieron cargar tus dominios. Intenta de nuevo.' });
  }
});

app.post('/api/domains', requireAuth, express.json(), async (req, res) => {
  const name = typeof req.body?.name === 'string' ? req.body.name.trim().toLowerCase() : '';
  const maxCacheBytes = req.body?.maxCacheBytes;
  const domainPattern = /^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/i;

  if (!domainPattern.test(name) || `_verificacion.${name}`.length > 253) {
    return res.status(400).json({ error: 'Escribe un dominio válido, sin protocolo, puerto ni rutas.' });
  }
  if (!Number.isSafeInteger(maxCacheBytes) || maxCacheBytes <= 0) {
    return res.status(400).json({ error: 'El límite de caché debe ser un número entero de bytes mayor que 0.' });
  }

  let originUrl;
  try {
    if (typeof req.body?.originUrl !== 'string') throw new Error();
    const origin = new URL(req.body.originUrl.trim());
    if (!['http:', 'https:'].includes(origin.protocol) || origin.username || origin.password
      || origin.pathname !== '/' || origin.search || origin.hash) throw new Error();
    originUrl = origin.origin;
  } catch {
    return res.status(400).json({ error: 'Escribe la dirección HTTP o HTTPS del servidor de origen, sin rutas, credenciales, parámetros ni fragmentos.' });
  }

  const domain = {
    ownerId: req.user.uid,
    name,
    maxCacheBytes,
    originUrl,
    verified: false,
    verificationToken: randomUUID(),
  };

  try {
    // El mismo dominio y cuenta usan el mismo documento para evitar duplicados.
    const ref = getDb().collection('domains').doc(`${encodeURIComponent(req.user.uid)}_${name}`);
    await ref.create(domain);
    res.status(201).json(domainResponse(ref.id, domain));
  } catch (error) {
    if (error.code === 6) return res.status(409).json({ error: 'Ya tienes registrado este dominio.' });
    res.status(503).json({ error: 'No se pudo guardar el dominio. Intenta de nuevo.' });
  }
});

async function loadOwnedDomain(req, res, next) {
  if (req.params.domainId.includes('/')) return res.status(404).json({ error: 'No se encontró este dominio en tu cuenta.' });
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
}

app.post('/api/domains/:domainId/verify', requireAuth, loadOwnedDomain, async (req, res) => {
  const domain = req.domainData;
  if (domain.verified) return res.json(domainResponse(req.domainRef.id, domain));
  try {
    if (!domain.verificationToken) throw new Error();
    const records = await dns.resolveTxt(`_verificacion.${domain.name}`);
    if (!records.some((parts) => parts.join('') === domain.verificationToken)) {
      return res.status(409).json({ error: 'El registro TXT no coincide. Revisa el nombre y el valor e intenta de nuevo.' });
    }
    const registered = await getDb().collection('domains').where('name', '==', domain.name).get();
    if (registered.docs.some((doc) => doc.id !== req.domainRef.id && doc.data().verified)) {
      return res.status(409).json({ error: 'Este dominio ya está verificado en otra cuenta.' });
    }
    await req.domainRef.update({ verified: true });
    res.json(domainResponse(req.domainRef.id, { ...domain, verified: true }));
  } catch (error) {
    if (['ENODATA', 'ENOTFOUND'].includes(error.code)) {
      return res.status(409).json({ error: 'El registro TXT todavía no aparece. Revisa la configuración del DNS e intenta de nuevo.' });
    }
    res.status(503).json({ error: 'No se pudo verificar el dominio. Intenta de nuevo.' });
  }
});

app.delete('/api/domains/:domainId', requireAuth, loadOwnedDomain, async (req, res) => {
  try {
    await getDb().recursiveDelete(req.domainRef);
    res.status(204).end();
  } catch {
    res.status(503).json({ error: 'No se pudo eliminar el dominio. Intenta de nuevo.' });
  }
});

app.use('/api/cache', cacheRouter);
app.use('/api/domains/:domainId/rules', rulesRouter);

app.use((req, res) => {
  res.status(404).json({ error: 'Ruta no encontrada' });
});

app.use((error, req, res, next) => {
  const status = [400, 413].includes(error.status) ? error.status : 500;
  res.status(status).json({ error: 'No se pudo procesar la solicitud.' });
});

export default app;
