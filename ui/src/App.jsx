import { useEffect, useState } from 'react';
import Login from './pages/Login.jsx';
import Register from './pages/Register.jsx';
import Domains from './pages/Domains.jsx';

export default function App() {
  const [route, setRoute] = useState(window.location.hash);
  const verificationId = route.match(/^#\/dominios\/([^/]+)\/verificar$/)?.[1];

  useEffect(() => {
    function handleNavigation() {
      setRoute(window.location.hash);
    }

    window.addEventListener('hashchange', handleNavigation);
    return () => window.removeEventListener('hashchange', handleNavigation);
  }, []);

  useEffect(() => {
    const titles = {
      '#/crear-cuenta': 'Crear cuenta',
      '#/dominios': 'Mis dominios',
    };
    const title = verificationId ? 'Verificar dominio' : titles[route] || 'Iniciar sesión';
    document.title = `${title} | Proxy cache`;
  }, [route, verificationId]);

  if (route === '#/crear-cuenta') return <Register />;
  if (route === '#/dominios' || verificationId) return <Domains verificationId={verificationId} />;
  return <Login />;
}
