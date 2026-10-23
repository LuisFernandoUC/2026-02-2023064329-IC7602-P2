import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from './App.jsx';
import { apiRequest } from './api.js';
import './index.css';

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <App />
  </StrictMode>,
);

apiRequest('/api/health')
  .then((data) => console.info('Estado de la API:', data.status))
  .catch((error) => console.error('No se pudo conectar con la API:', error.message));
