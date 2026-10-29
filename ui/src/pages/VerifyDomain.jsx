import { useState } from 'react';

export default function VerifyDomain({ domain }) {
  const [message, setMessage] = useState('');

  async function copyRecord(value, label) {
    try {
      await navigator.clipboard.writeText(value);
      setMessage(`${label} copiado al portapapeles.`);
    } catch {
      setMessage('No se pudo copiar. Selecciona el texto y cópialo manualmente.');
    }
  }

  if (!domain?.verificationRecord) {
    return (
      <>
        <a href="#/dominios" className="text-link text-sm">← Volver a Mis dominios</a>
        <h1 className="mt-6 text-3xl font-semibold">Verificar dominio</h1>
        <p className="mt-4 text-muted">No hay un registro TXT disponible para este dominio en la vista de ejemplo.</p>
      </>
    );
  }

  const { name, value } = domain.verificationRecord;

  return (
    <>
      <a href="#/dominios" className="text-link text-sm">← Volver a Mis dominios</a>

      <div className="mt-6 border-b border-line pb-7">
        <h1 className="text-3xl font-semibold tracking-tight">Verificar dominio</h1>
        <div className="mt-4 flex flex-wrap items-center gap-3">
          <span className="font-mono text-lg font-semibold">{domain.name}</span>
          <span className="rounded-full border border-amber-200 bg-amber-50 px-2.5 py-1 text-xs font-medium text-amber-800">Pendiente</span>
        </div>
      </div>

      <section className="mt-7 max-w-3xl rounded-xl border border-line bg-surface p-6" aria-labelledby="txt-title">
        <h2 id="txt-title" className="text-lg font-semibold">Agrega un registro TXT en tu proveedor de DNS</h2>
        <p className="mt-3 text-sm leading-6 text-muted">Crea un registro de tipo TXT con el nombre y el valor asignados a tu dominio. Después vuelve aquí para verificar su propiedad.</p>

        <p className="form-message mt-5">Datos de ejemplo. No agregues este registro a tu DNS.</p>

        <dl className="mt-6 space-y-5">
          <div>
            <dt className="field-label">Nombre del registro / Host</dt>
            <dd className="flex items-center gap-3 rounded border border-line bg-background p-3">
              <code className="min-w-0 flex-1 break-all text-sm">{name}</code>
              <button type="button" onClick={() => copyRecord(name, 'Nombre')} aria-label="Copiar nombre del registro" className="text-link shrink-0 px-2 py-1 text-sm font-medium">Copiar</button>
            </dd>
          </div>
          <div>
            <dt className="field-label">Tipo</dt>
            <dd className="font-mono text-sm">TXT</dd>
          </div>
          <div>
            <dt className="field-label">Valor / Contenido TXT</dt>
            <dd className="flex items-center gap-3 rounded border border-line bg-background p-3">
              <code className="min-w-0 flex-1 break-all text-sm">{value}</code>
              <button type="button" onClick={() => copyRecord(value, 'Valor')} aria-label="Copiar valor TXT" className="text-link shrink-0 px-2 py-1 text-sm font-medium">Copiar</button>
            </dd>
          </div>
        </dl>

        <p className="mt-6 text-sm leading-6 text-muted">Los cambios de DNS pueden tardar en reflejarse. Si el registro todavía no aparece, podrás volver a intentarlo.</p>
        {message && <p role="status" className="form-message mt-5">{message}</p>}

        <div className="mt-6 border-t border-line pt-5">
          <button type="button" className="button-primary" onClick={() => setMessage('La verificación TXT aún no está disponible.')}>Verificar dominio</button>
        </div>
      </section>
    </>
  );
}
