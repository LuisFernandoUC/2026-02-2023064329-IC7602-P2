import { useState } from 'react';
import PasswordField from './PasswordField.jsx';
import DeleteDialog from './DeleteDialog.jsx';

export default function RuleCredentials({ rule, authMode }) {
  const [editingUser, setEditingUser] = useState(null);
  const [deleting, setDeleting] = useState(null);
  const [message, setMessage] = useState('');
  const isApiKey = authMode === 'api-key';
  const items = isApiKey ? rule.keys ?? [] : rule.users ?? [];

  function handleUserSubmit(event) {
    event.preventDefault();
    const input = event.currentTarget.elements.namedItem('resource-username');
    const username = input.value.trim();

    if (!username) {
      input.setCustomValidity('Escribe un nombre de usuario.');
      input.reportValidity();
      return;
    }

    if (items.some((user) => user.id !== editingUser.id && user.username === username)) {
      input.setCustomValidity('Este nombre de usuario ya existe en la regla.');
      input.reportValidity();
      return;
    }

    setMessage(editingUser.id ? 'Guardar usuarios aún no está disponible.' : 'Crear usuarios aún no está disponible.');
  }

  return (
    <section className="rounded-xl border border-line bg-surface p-6" aria-labelledby="credentials-title">
      <div className="flex flex-wrap items-center justify-between gap-4">
        <h2 id="credentials-title" className="text-lg font-semibold">{isApiKey ? 'Claves de acceso' : 'Usuarios de acceso'}</h2>
        {isApiKey ? (
          <button type="button" onClick={() => setMessage('Crear claves aún no está disponible.')} className="text-link text-sm font-medium">+ Crear clave</button>
        ) : (
          <button type="button" onClick={() => { setEditingUser({ id: null, username: '' }); setMessage(''); }} className="text-link text-sm font-medium">+ Agregar usuario</button>
        )}
      </div>

      <p className="mt-3 text-sm leading-6 text-muted">
        {isApiKey ? 'Las claves existentes no muestran su valor. Una clave nueva se mostrará solo al crearla.' : 'Estos usuarios permiten acceder a los archivos de esta regla. Son distintos de la cuenta del panel.'}
      </p>

      {items.length === 0 ? (
        <p className="mt-5 rounded bg-soft p-4 text-sm text-muted">{isApiKey ? 'Esta regla no tiene claves.' : 'Esta regla no tiene usuarios.'}</p>
      ) : (
        <ul className="mt-5 divide-y divide-line rounded-lg border border-line" aria-label={isApiKey ? 'Claves de ejemplo' : 'Usuarios de ejemplo'}>
          {items.map((item) => (
            <li key={item.id} className="flex flex-wrap items-center justify-between gap-4 p-4">
              <span className="break-all font-mono text-sm font-medium">{isApiKey ? item.id : item.username}</span>
              <div className="flex items-center gap-4 text-sm">
                {!isApiKey && <button type="button" onClick={() => { setEditingUser(item); setMessage(''); }} aria-label={`Editar usuario ${item.username}`} className="text-link">Editar</button>}
                <button type="button" onClick={() => { setDeleting({ id: item.id, name: isApiKey ? item.id : item.username }); setMessage(''); }} aria-label={`Eliminar ${isApiKey ? 'clave' : 'usuario'} ${isApiKey ? item.id : item.username}`} className="rounded text-red-700 hover:underline">Eliminar</button>
              </div>
            </li>
          ))}
        </ul>
      )}

      {!isApiKey && editingUser && (
        <form
          key={editingUser.id ?? 'new'}
          onSubmit={handleUserSubmit}
          onInput={(event) => { event.target.setCustomValidity(''); setMessage(''); }}
          className="mt-6 space-y-5 rounded-lg bg-soft p-5"
          aria-labelledby="user-form-title"
        >
          <h3 id="user-form-title" className="font-semibold">{editingUser.id ? 'Editar usuario' : 'Agregar usuario'}</h3>
          <div className="grid items-start gap-5 md:grid-cols-2">
            <div>
              <label htmlFor="resource-username" className="field-label">Nombre de usuario</label>
              <input id="resource-username" name="resource-username" type="text" autoComplete="off" defaultValue={editingUser.username} required className="field-input" />
            </div>
            <div>
              <PasswordField id="resource-password" label={editingUser.id ? 'Nueva contraseña' : 'Contraseña'} autoComplete="new-password" required={!editingUser.id} />
              {editingUser.id && <p className="mt-2 text-xs text-muted">Déjala vacía para conservar la contraseña actual.</p>}
            </div>
          </div>
          <div className="flex justify-end gap-3">
            <button type="button" onClick={() => { setEditingUser(null); setMessage(''); }} className="rounded border border-line bg-surface px-4 py-2.5 text-sm font-medium hover:bg-background">Cancelar usuario</button>
            <button type="submit" className="button-primary">{editingUser.id ? 'Guardar usuario' : 'Agregar usuario'}</button>
          </div>
        </form>
      )}

      {message && <p role="status" className="form-message mt-5">{message}</p>}

      {deleting && (
        <DeleteDialog
          title={`¿Eliminar ${isApiKey ? 'clave' : 'usuario'}?`}
          name={deleting.name}
          description={isApiKey ? 'Al eliminarla, esta clave dejará de permitir el acceso a la regla.' : 'Al eliminarlo, este usuario dejará de tener acceso a la regla.'}
          confirmLabel={`Eliminar ${isApiKey ? 'clave' : 'usuario'}`}
          onClose={() => setDeleting(null)}
          onConfirm={() => setMessage(isApiKey ? 'Eliminar claves aún no está disponible.' : 'Eliminar usuarios aún no está disponible.')}
        />
      )}
    </section>
  );
}
