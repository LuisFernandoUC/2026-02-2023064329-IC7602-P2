import { useState } from 'react';
import { AuthLayout } from '../components/Layout.jsx';
import PasswordField from '../components/PasswordField.jsx';

export default function Register() {
  const [message, setMessage] = useState('');

  function handleSubmit(event) {
    event.preventDefault();
    const form = event.currentTarget;
    const data = new FormData(form);

    if (data.get('password') !== data.get('confirm-password')) {
      setMessage('Las contraseñas no coinciden.');
      form.elements.namedItem('confirm-password').focus();
      return;
    }

    setMessage('El registro de cuentas aún no está disponible.');
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
        {message && <p role="status" className="form-message">{message}</p>}
        <button type="submit" className="button-primary w-full">Crear cuenta</button>
      </form>
    </AuthLayout>
  );
}
