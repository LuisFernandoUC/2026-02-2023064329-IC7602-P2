import { useState } from 'react';
import { AuthLayout } from '../components/Layout.jsx';
import PasswordField from '../components/PasswordField.jsx';

const exampleResource = {
  domain: 'example.com',
  path: '/privado/documento.pdf',
};

export default function ResourceAccess() {
  const [message, setMessage] = useState('');

  function handleSubmit(event) {
    event.preventDefault();
    const usernameInput = event.currentTarget.elements.namedItem('resource-username');

    if (!usernameInput.value.trim()) {
      usernameInput.setCustomValidity('Escribe tu nombre de usuario.');
      usernameInput.reportValidity();
      return;
    }

    setMessage('El acceso al recurso aún no está disponible.');
  }

  return (
    <AuthLayout
      title="Acceder al recurso"
      description="Ingresa tus credenciales para abrir el archivo solicitado."
      brandHref="#/acceso-recurso"
      footer="Vista con un recurso de ejemplo."
    >
      <dl className="mb-6 space-y-4 rounded-lg bg-soft p-4 text-sm">
        <div>
          <dt className="text-muted">Dominio</dt>
          <dd className="mt-1 break-all font-mono font-medium">{exampleResource.domain}</dd>
        </div>
        <div>
          <dt className="text-muted">Ruta solicitada</dt>
          <dd className="mt-1 break-all font-mono">{exampleResource.path}</dd>
        </div>
      </dl>

      <form
        onSubmit={handleSubmit}
        onInput={(event) => { event.target.setCustomValidity(''); setMessage(''); }}
        className="space-y-5"
      >
        <div>
          <label htmlFor="resource-username" className="field-label">Usuario</label>
          <input id="resource-username" name="resource-username" type="text" autoComplete="username" required className="field-input" />
        </div>
        <PasswordField id="resource-password" autoComplete="current-password" />
        {message && <p role="status" className="form-message">{message}</p>}
        <button type="submit" className="button-primary w-full">Acceder</button>
      </form>
    </AuthLayout>
  );
}
