import { useEffect, useState } from 'react';
import { onAuthStateChanged } from 'firebase/auth';
import { getPanelAuth } from './auth.js';
import Login from './pages/Login.jsx';
import Register from './pages/Register.jsx';
import Domains from './pages/Domains.jsx';
import ResourceAccess from './pages/ResourceAccess.jsx';

export default function App() {
  const [route, setRoute] = useState(window.location.hash);
  const [user, setUser] = useState(null);
  const [authReady, setAuthReady] = useState(false);
  const [authError, setAuthError] = useState('');
  const isPanelRoute = route === '#/dominios' || route.startsWith('#/dominios/');
  const verificationId = route.match(/^#\/dominios\/([^/]+)\/verificar$/)?.[1];
  const domainId = route.match(/^#\/dominios\/([^/]+)$/)?.[1];
  const ruleRoute = route.match(/^#\/dominios\/([^/]+)\/reglas\/(?:nueva|([^/]+)\/editar)$/);
  const titles = {
    '#/crear-cuenta': 'Crear cuenta',
    '#/dominios': 'Mis dominios',
    '#/acceso-recurso': 'Acceder al recurso',
  };
  let pageTitle = titles[route] || 'Iniciar sesión';
  if (verificationId) pageTitle = 'Verificar dominio';
  if (domainId) pageTitle = 'Detalle del dominio';
  if (ruleRoute) pageTitle = ruleRoute[2] ? 'Editar regla' : 'Agregar regla';

  useEffect(() => {
    function handleNavigation() {
      setRoute(window.location.hash);
    }

    window.addEventListener('hashchange', handleNavigation);
    return () => window.removeEventListener('hashchange', handleNavigation);
  }, []);

  useEffect(() => {
    document.title = `${pageTitle} | Proxy cache`;
  }, [pageTitle]);

  useEffect(() => {
    try {
      return onAuthStateChanged(getPanelAuth(), (currentUser) => {
        setUser(currentUser);
        setAuthReady(true);
      }, () => {
        setAuthError('No se pudo comprobar la sesión. Recarga la página.');
        setAuthReady(true);
      });
    } catch (error) {
      setAuthError(error.message);
      setAuthReady(true);
    }
  }, []);

  useEffect(() => {
    if (!authReady || authError || route === '#/acceso-recurso') return;
    if (user && !isPanelRoute) window.location.hash = '#/dominios';
    if (!user && isPanelRoute) window.location.hash = '#/iniciar-sesion';
  }, [user, authReady, authError, route, isPanelRoute]);

  if (route === '#/acceso-recurso') return <ResourceAccess />;
  if (!authReady) return <p role="status" className="p-6 text-muted">Comprobando sesión…</p>;
  if (authError) return <p role="alert" className="p-6">{authError}</p>;
  if (!user) return route === '#/crear-cuenta' ? <Register /> : <Login />;

  if (isPanelRoute) {
    return <Domains key={user.uid} user={user} verificationId={verificationId} domainId={domainId} ruleForm={ruleRoute ? { domainId: ruleRoute[1], ruleId: ruleRoute[2] } : null} />;
  }
  return <p role="status" className="p-6 text-muted">Abriendo dominios…</p>;
}
