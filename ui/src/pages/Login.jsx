import { useState } from 'react';
import { signInWithEmailAndPassword } from 'firebase/auth';
import { authErrorMessage, getPanelAuth } from '../auth.js';
import { AuthLayout } from '../components/Layout.jsx';
import PasswordField from '../components/PasswordField.jsx';

export default function Login() {
  const [message, setMessage] = useState('');
  const [submitting, setSubmitting] = useState(false);

  async function handleSubmit(event) {
    event.preventDefault();
    const data = new FormData(event.currentTarget);
    setMessage('');
    setSubmitting(true);

    try {
      await signInWithEmailAndPassword(getPanelAuth(), data.get('email').trim(), data.get('password'));
    } catch (error) {
      setMessage(authErrorMessage(error));
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <AuthLayout
      title="Iniciar sesión"
      description="Accede para administrar tus dominios y reglas de caché."
      footer={<>¿No tienes cuenta? <a href="#/crear-cuenta" className="text-link font-semibold">Crear cuenta</a></>}
    >
      <form onSubmit={handleSubmit} onInput={() => setMessage('')} className="space-y-5">
        <div>
          <label htmlFor="email" className="field-label">Correo electrónico</label>
          <input id="email" name="email" type="email" autoComplete="email" placeholder="ejemplo@dominio.com" className="field-input" required />
        </div>
        <PasswordField id="password" autoComplete="current-password" />
        {message && <p role="alert" className="form-message">{message}</p>}
        <button type="submit" disabled={submitting} className="button-primary w-full">{submitting ? 'Iniciando sesión…' : 'Iniciar sesión'} <span aria-hidden="true">→</span></button>
      </form>
    </AuthLayout>
  );
}
