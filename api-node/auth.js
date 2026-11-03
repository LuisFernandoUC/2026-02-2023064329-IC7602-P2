import { getAuth } from 'firebase-admin/auth';
import { getFirebaseApp } from './firebase.js';

export async function requireAuth(req, res, next) {
  const token = req.get('Authorization')?.match(/^Bearer (\S+)$/i)?.[1];

  if (!token) {
    return res.status(401).json({ error: 'Debes iniciar sesión.' });
  }

  try {
    req.user = await getAuth(getFirebaseApp()).verifyIdToken(token);
  } catch (error) {
    const invalidToken = [
      'auth/argument-error', 'auth/invalid-argument', 'auth/invalid-id-token',
      'auth/id-token-expired', 'auth/id-token-revoked', 'auth/user-disabled',
    ].includes(error.code);

    return res.status(invalidToken ? 401 : 503).json({
      error: invalidToken ? 'La sesión no es válida o venció. Inicia sesión de nuevo.' : 'No se pudo validar la sesión. Intenta de nuevo.',
    });
  }

  next();
}
