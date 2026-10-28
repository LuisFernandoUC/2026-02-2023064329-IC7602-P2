import { useState } from 'react';
import { AuthLayout } from '../components/Layout.jsx';
import PasswordField from '../components/PasswordField.jsx';

export default function Login() {
  const [message, setMessage] = useState('');

  function handleSubmit(event) {
    event.preventDefault();
    setMessage('El inicio de sesión aún no está disponible.');
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
        {message && <p role="status" className="form-message">{message}</p>}
        <button type="submit" className="button-primary w-full">Iniciar sesión <span aria-hidden="true">→</span></button>
      </form>
    </AuthLayout>
  );
}
