import { useEffect, useState } from 'react';
import Login from './pages/Login.jsx';
import Register from './pages/Register.jsx';
import Domains from './pages/Domains.jsx';

export default function App() {
  const [route, setRoute] = useState(window.location.hash);

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
    document.title = `${titles[route] || 'Iniciar sesión'} | Proxy cache`;
  }, [route]);

  if (route === '#/crear-cuenta') return <Register />;
  if (route === '#/dominios') return <Domains />;
  return <Login />;
}
