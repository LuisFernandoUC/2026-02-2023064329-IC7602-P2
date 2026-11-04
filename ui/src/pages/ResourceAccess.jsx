import { useState } from 'react';
import { AuthLayout } from '../components/Layout.jsx';
import PasswordField from '../components/PasswordField.jsx';
import { apiRequest } from '../api.js';

export default function ResourceAccess() {
  const [message, setMessage] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const queryStart = window.location.hash.indexOf('?');
  const params = new URLSearchParams(queryStart < 0 ? '' : window.location.hash.slice(queryStart + 1));
  const resource = { domain: params.get('domain') ?? '', path: params.get('path') ?? '', callback: params.get('callback') ?? '' };
  let callbackUrl;
  try {
    const url = new URL(resource.callback);
    if (['http:', 'https:'].includes(url.protocol) && url.hostname === resource.domain.toLowerCase()
      && url.pathname === '/__zc/auth' && !url.username && !url.password && !url.hash && resource.path.startsWith('/')) callbackUrl = url;
  } catch { /* Un enlace incompleto no muestra el formulario. */ }

  async function handleSubmit(event) {
    event.preventDefault();
    const usernameInput = event.currentTarget.elements.namedItem('resource-username');

    if (!usernameInput.value.trim()) {
      usernameInput.setCustomValidity('Escribe tu nombre de usuario.');
      usernameInput.reportValidity();
      return;
    }

    const password = event.currentTarget.elements.namedItem('resource-password').value;
    setSubmitting(true);
    setMessage('');
    try {
      const { redirect } = await apiRequest('/api/cache/sessions', {
        panelSession: false,
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ...resource, username: usernameInput.value.trim(), password }),
      });
      window.location.assign(redirect);
    } catch (error) {
      setMessage(error.message);
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <AuthLayout
      title="Acceder al recurso"
      description="Ingresa tus credenciales para abrir el archivo solicitado."
      brandHref="#/acceso-recurso"
      footer={callbackUrl ? <a href={`${callbackUrl.origin}/__zc/logout`} className="text-link">Cerrar sesión del recurso</a> : 'Abre el recurso desde su dirección original.'}
    >
      {callbackUrl ? (
        <>
          <dl className="mb-6 space-y-4 rounded-lg bg-soft p-4 text-sm">
            <div>
              <dt className="text-muted">Dominio</dt>
              <dd className="mt-1 break-all font-mono font-medium">{resource.domain}</dd>
            </div>
            <div>
              <dt className="text-muted">Ruta solicitada</dt>
              <dd className="mt-1 break-all font-mono">{resource.path}</dd>
            </div>
          </dl>
          <form
            onSubmit={handleSubmit}
            onInput={(event) => { event.target.setCustomValidity(''); setMessage(''); }}
          >
            <fieldset disabled={submitting} className="space-y-5">
              <div>
                <label htmlFor="resource-username" className="field-label">Usuario</label>
                <input id="resource-username" name="resource-username" type="text" autoComplete="username" required className="field-input" />
              </div>
              <PasswordField id="resource-password" autoComplete="current-password" />
              {message && <p role="alert" className="form-message">{message}</p>}
              <button type="submit" disabled={submitting} className="button-primary w-full">{submitting ? 'Accediendo…' : 'Acceder'}</button>
            </fieldset>
          </form>
        </>
      ) : <p role="status" className="text-sm text-muted">No hay un recurso válido para iniciar sesión.</p>}
    </AuthLayout>
  );
}
