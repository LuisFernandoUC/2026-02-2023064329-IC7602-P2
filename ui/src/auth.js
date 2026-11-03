import { getApp, getApps, initializeApp } from 'firebase/app';
import { getAuth } from 'firebase/auth';

export function getPanelAuth() {
  const config = {
    apiKey: import.meta.env.VITE_FIREBASE_API_KEY,
    authDomain: import.meta.env.VITE_FIREBASE_AUTH_DOMAIN,
    projectId: import.meta.env.VITE_FIREBASE_PROJECT_ID,
    appId: import.meta.env.VITE_FIREBASE_APP_ID,
  };

  if (Object.values(config).some((value) => !value)) {
    throw new Error('Falta configurar Firebase en ui/.env.');
  }

  return getAuth(getApps().length ? getApp() : initializeApp(config));
}

export function authErrorMessage(error) {
  const messages = {
    'auth/invalid-credential': 'Correo o contraseña incorrectos.',
    'auth/wrong-password': 'Correo o contraseña incorrectos.',
    'auth/user-not-found': 'Correo o contraseña incorrectos.',
    'auth/invalid-email': 'Escribe un correo válido.',
    'auth/email-already-in-use': 'Ese correo ya tiene una cuenta.',
    'auth/weak-password': 'Usa una contraseña de al menos 6 caracteres.',
    'auth/too-many-requests': 'Se hicieron demasiados intentos. Intenta más tarde.',
    'auth/network-request-failed': 'No se pudo conectar. Revisa tu conexión e intenta de nuevo.',
    'auth/user-disabled': 'Esta cuenta no está habilitada.',
    'auth/operation-not-allowed': 'El acceso con correo y contraseña no está habilitado.',
  };

  return messages[error.code] || 'No se pudo completar el acceso. Intenta de nuevo.';
}
