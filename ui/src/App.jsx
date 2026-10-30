import { useEffect, useState } from 'react';
import Login from './pages/Login.jsx';
import Register from './pages/Register.jsx';
import Domains from './pages/Domains.jsx';
import ResourceAccess from './pages/ResourceAccess.jsx';

export default function App() {
  const [route, setRoute] = useState(window.location.hash);
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

  if (route === '#/crear-cuenta') return <Register />;
  if (route === '#/acceso-recurso') return <ResourceAccess />;
  if (route === '#/dominios' || verificationId || domainId || ruleRoute) {
    return <Domains verificationId={verificationId} domainId={domainId} ruleForm={ruleRoute ? { domainId: ruleRoute[1], ruleId: ruleRoute[2] } : null} />;
  }
  return <Login />;
}
