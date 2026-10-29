import { useEffect, useRef, useState } from 'react';

export default function AddDomainDialog({ onClose }) {
  const dialogRef = useRef(null);
  const [message, setMessage] = useState('');

  useEffect(() => {
    dialogRef.current.showModal();
  }, []);

  function closeDialog() {
    dialogRef.current.close();
  }

  function handleSubmit(event) {
    event.preventDefault();
    const form = event.currentTarget;
    const domainInput = form.elements.namedItem('domain');
    const cacheInput = form.elements.namedItem('cache-limit');
    const domain = domainInput.value.trim();
    const domainPattern = /^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$/i;

    if (domain.length > 253 || !domainPattern.test(domain)) {
      domainInput.setCustomValidity('Escribe un dominio como ejemplo.com, sin protocolo, puerto ni rutas.');
      domainInput.reportValidity();
      return;
    }

    if (!Number.isFinite(cacheInput.valueAsNumber) || cacheInput.valueAsNumber <= 0) {
      cacheInput.setCustomValidity('El límite de caché debe ser mayor que 0 MB.');
      cacheInput.reportValidity();
      return;
    }

    setMessage('Guardar dominios aún no está disponible.');
  }

  return (
    <dialog
      ref={dialogRef}
      onClose={onClose}
      aria-labelledby="add-domain-title"
      aria-describedby="add-domain-description"
      className="m-auto max-h-[calc(100vh-3rem)] w-[calc(100%-3rem)] max-w-lg overflow-y-auto rounded-xl bg-surface p-0 text-ink shadow-xl backdrop:bg-ink/40"
    >
      <div className="flex items-start justify-between gap-4 border-b border-line p-6">
        <div>
          <h2 id="add-domain-title" className="text-xl font-semibold">Agregar dominio</h2>
          <p id="add-domain-description" className="mt-2 text-sm leading-6 text-muted">Ingresa el dominio sin protocolo ni rutas.</p>
        </div>
        <button type="button" onClick={closeDialog} aria-label="Cerrar formulario" className="flex h-8 w-8 shrink-0 items-center justify-center rounded text-2xl leading-none text-muted hover:bg-soft">×</button>
      </div>

      <form
        onSubmit={handleSubmit}
        onInput={(event) => {
          event.target.setCustomValidity('');
          setMessage('');
        }}
        className="space-y-5 p-6"
      >
        <div>
          <label htmlFor="domain" className="field-label">Nombre del dominio</label>
          <input id="domain" name="domain" type="text" placeholder="ejemplo.com" autoCapitalize="none" spellCheck={false} autoFocus required className="field-input" aria-describedby="domain-help" />
          <p id="domain-help" className="mt-2 text-xs text-muted">Puedes usar un dominio o subdominio, como archivos.ejemplo.com.</p>
        </div>

        <div>
          <label htmlFor="cache-limit" className="field-label">Límite total de caché</label>
          <div className="relative">
            <input id="cache-limit" name="cache-limit" type="number" min="0" step="any" required className="field-input pr-12" aria-describedby="cache-unit" />
            <span id="cache-unit" className="pointer-events-none absolute inset-y-0 right-3 flex items-center text-sm text-muted">MB</span>
          </div>
        </div>

        <p className="rounded bg-soft p-3 text-sm leading-6 text-muted">Después de guardar el dominio, podrás verificar su propiedad con un registro TXT.</p>
        {message && <p role="status" className="form-message">{message}</p>}

        <div className="flex justify-end gap-3 border-t border-line pt-5">
          <button type="button" onClick={closeDialog} className="rounded border border-line px-4 py-2.5 text-sm font-medium hover:bg-soft">Cancelar</button>
          <button type="submit" className="button-primary">Agregar dominio</button>
        </div>
      </form>
    </dialog>
  );
}
