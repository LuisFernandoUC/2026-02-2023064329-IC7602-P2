import { useState } from 'react';
import { createUserWithEmailAndPassword } from 'firebase/auth';
import { authErrorMessage, getPanelAuth } from '../auth.js';
import { AuthLayout } from '../components/Layout.jsx';
import PasswordField from '../components/PasswordField.jsx';

export default function Register() {
  const [message, setMessage] = useState('');
  const [submitting, setSubmitting] = useState(false);

  async function handleSubmit(event) {
    event.preventDefault();
    const form = event.currentTarget;
    const data = new FormData(form);

    if (data.get('password') !== data.get('confirm-password')) {
      setMessage('Las contraseñas no coinciden.');
      form.elements.namedItem('confirm-password').focus();
      return;
    }

    setMessage('');
    setSubmitting(true);

    try {
      await createUserWithEmailAndPassword(getPanelAuth(), data.get('email').trim(), data.get('password'));
    } catch (error) {
      setMessage(authErrorMessage(error));
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <AuthLayout
      title="Crear cuenta"
      description="Crea una cuenta para administrar tus dominios."
      footer={<>¿Ya tienes cuenta? <a href="#/iniciar-sesion" className="text-link font-semibold">Iniciar sesión</a></>}
    >
      <form onSubmit={handleSubmit} onInput={() => setMessage('')} className="space-y-5">
        <div>
          <label htmlFor="email" className="field-label">Correo electrónico</label>
          <input id="email" name="email" type="email" autoComplete="email" placeholder="ejemplo@dominio.com" className="field-input" required />
        </div>
        <PasswordField id="password" autoComplete="new-password" />
        <PasswordField id="confirm-password" label="Confirmar contraseña" autoComplete="new-password" />
        {message && <p role="alert" className="form-message">{message}</p>}
        <button type="submit" disabled={submitting} className="button-primary w-full">{submitting ? 'Creando cuenta…' : 'Crear cuenta'}</button>
      </form>
    </AuthLayout>
  );
}
