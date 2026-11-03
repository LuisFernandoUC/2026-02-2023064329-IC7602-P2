import { useEffect, useRef, useState } from 'react';
import { apiRequest } from '../api.js';
import RuleCredentials from '../components/RuleCredentials.jsx';

const replacementPolicies = ['LRU', 'LFU', 'FIFO', 'MRU', 'Random'];

export default function RuleForm({ domain, ruleId }) {
  const [rule, setRule] = useState(null);
  const [fileTypes, setFileTypes] = useState([{ rowId: 0, extension: '', ttlSeconds: '', replacementPolicy: 'LRU' }]);
  const nextRowId = useRef(fileTypes.length);
  const formRef = useRef(null);
  const [message, setMessage] = useState('');
  const [authMode, setAuthMode] = useState('api-key');
  const [loading, setLoading] = useState(Boolean(ruleId));
  const [loadError, setLoadError] = useState('');
  const [submitting, setSubmitting] = useState(false);

  useEffect(() => {
    if (!domain || !ruleId) return;
    let active = true;
    apiRequest(`/api/domains/${encodeURIComponent(domain.id)}/rules`)
      .then((data) => {
        if (!active) return;
        const saved = data.find((item) => item.id === ruleId);
        if (!saved) throw new Error('No se encontró esta regla en el dominio.');
        setRule(saved);
        setAuthMode(saved.authMode);
        setFileTypes(saved.fileTypes.map((type, index) => ({ ...type, rowId: index })));
        nextRowId.current = saved.fileTypes.length;
      })
      .catch((error) => { if (active) setLoadError(error.message); })
      .finally(() => { if (active) setLoading(false); });
    return () => { active = false; };
  }, [domain?.id, ruleId]);

  if (!domain) {
    return (
      <>
        <a href="#/dominios" className="text-link text-sm">← Volver a Mis dominios</a>
        <h1 className="mt-6 text-3xl font-semibold">Regla no disponible</h1>
        <p className="mt-4 text-muted">No se encontró este dominio en tu cuenta.</p>
      </>
    );
  }

  const backUrl = `#/dominios/${domain.id}`;

  if (loading || loadError) {
    return (
      <>
        <a href={backUrl} className="text-link text-sm">← Volver a {domain.name}</a>
        <p role={loadError ? 'alert' : 'status'} className="form-message mt-6">{loadError || 'Cargando regla…'}</p>
      </>
    );
  }

  function clearValidation() {
    for (const field of formRef.current.elements) {
      field.setCustomValidity?.('');
    }
    setMessage('');
  }

  function addFileType() {
    const newType = { rowId: nextRowId.current++, extension: '', ttlSeconds: '', replacementPolicy: 'LRU' };
    setFileTypes([...fileTypes, newType]);
    clearValidation();
  }

  function updateFileType(rowId, field, value) {
    setFileTypes(fileTypes.map((type) => type.rowId === rowId ? { ...type, [field]: value } : type));
  }

  function showFieldError(input, text) {
    input.setCustomValidity(text);
    input.reportValidity();
  }

  async function handleSubmit(event) {
    event.preventDefault();
    const form = event.currentTarget;
    const pathInput = form.elements.namedItem('path-pattern');
    const cacheInput = form.elements.namedItem('cache-size');
    const path = pathInput.value.trim();

    if (!path.startsWith('/') || /\s/.test(path)) {
      showFieldError(pathInput, 'Escribe una ruta que empiece con / y no tenga espacios, como /imagenes/*.');
      return;
    }

    if (!Number.isFinite(cacheInput.valueAsNumber) || cacheInput.valueAsNumber <= 0) {
      showFieldError(cacheInput, 'El tamaño de caché debe ser mayor que 0 MB.');
      return;
    }

    if (fileTypes.length === 0) {
      setMessage('Agrega al menos un tipo de archivo.');
      return;
    }

    const extensions = new Set();
    const normalizedTypes = [];

    for (const type of fileTypes) {
      const extensionInput = form.elements.namedItem(`extension-${type.rowId}`);
      const ttlInput = form.elements.namedItem(`ttl-${type.rowId}`);
      const extension = extensionInput.value.trim().replace(/^\./, '').toLowerCase();

      if (!/^[a-z0-9]+(?:[.-][a-z0-9]+)*$/.test(extension)) {
        showFieldError(extensionInput, 'Escribe una extensión como png, jpg o pdf, sin rutas ni espacios.');
        return;
      }

      if (extensions.has(extension)) {
        showFieldError(extensionInput, 'Este tipo de archivo ya está agregado.');
        return;
      }

      if (!Number.isInteger(ttlInput.valueAsNumber) || ttlInput.valueAsNumber < 0) {
        showFieldError(ttlInput, 'El TTL debe ser un número entero de segundos, mayor o igual que 0.');
        return;
      }

      extensions.add(extension);
      normalizedTypes.push({ extension, ttlSeconds: ttlInput.valueAsNumber, replacementPolicy: type.replacementPolicy });
    }

    setSubmitting(true);
    setMessage('');
    try {
      const path = `/api/domains/${encodeURIComponent(domain.id)}/rules${ruleId ? `/${encodeURIComponent(ruleId)}` : ''}`;
      await apiRequest(path, {
        method: ruleId ? 'PUT' : 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ pathPattern: pathInput.value.trim(), maxCacheBytes: Math.round(cacheInput.valueAsNumber * 1024 * 1024), authMode, fileTypes: normalizedTypes }),
      });
      window.location.hash = backUrl;
    } catch (error) {
      setMessage(error.message);
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <>
      <a href={backUrl} className="text-link text-sm">← Volver a {domain.name}</a>
      <div className="mt-6 border-b border-line pb-6">
        <h1 className="text-3xl font-semibold tracking-tight">{rule ? 'Editar regla de URL' : 'Agregar regla de URL'}</h1>
        <p className="mt-3 text-sm text-muted">Dominio: <span className="font-mono font-medium text-ink">{domain.name}</span></p>
      </div>

      <div className="mt-6 max-w-5xl space-y-6">
        <form
          id="rule-form"
          ref={formRef}
          className="space-y-6"
          onSubmit={handleSubmit}
          onInput={clearValidation}
        >
          <section className="rounded-xl border border-line bg-surface p-6" aria-labelledby="route-title">
            <h2 id="route-title" className="text-lg font-semibold">Ruta y tamaño de caché</h2>
            <div className="mt-5 grid gap-5 md:grid-cols-2">
              <div>
                <label htmlFor="path-pattern" className="field-label">Ruta o patrón</label>
                <input id="path-pattern" name="path-pattern" type="text" defaultValue={rule?.pathPattern ?? ''} placeholder="/imagenes/*" required className="field-input font-mono" aria-describedby="path-help" />
                <p id="path-help" className="mt-2 text-xs text-muted">La ruta debe empezar con /. Puedes usar * como comodín.</p>
              </div>
              <div>
                <label htmlFor="cache-size" className="field-label">Tamaño máximo de caché (MB)</label>
                <input id="cache-size" name="cache-size" type="number" min="0" step="any" defaultValue={rule ? rule.maxCacheBytes / (1024 * 1024) : ''} required className="field-input" aria-describedby="cache-help" />
                <p id="cache-help" className="mt-2 text-xs text-muted">El límite total del dominio también se aplica: {domain.cacheSize}.</p>
              </div>
            </div>
          </section>

          <section className="rounded-xl border border-line bg-surface p-6" aria-labelledby="file-types-title">
            <div className="flex flex-wrap items-center justify-between gap-4">
              <div>
                <h2 id="file-types-title" className="text-lg font-semibold">Tipos de archivo y políticas</h2>
                <p className="mt-2 text-sm text-muted">Define el TTL en segundos y la política de reemplazo de cada extensión.</p>
              </div>
              <button type="button" onClick={addFileType} className="text-link text-sm font-medium">+ Agregar tipo de archivo</button>
            </div>

            {fileTypes.length === 0 ? (
              <p className="mt-5 rounded bg-soft p-4 text-sm text-muted">No hay tipos de archivo. Agrega al menos uno.</p>
            ) : (
              <div className="mt-5 overflow-x-auto">
                <table className="w-full min-w-[620px] text-left text-sm">
                  <caption className="sr-only">Tipos de archivo de la regla</caption>
                  <thead className="text-xs uppercase tracking-wide text-muted">
                    <tr>
                      <th scope="col" className="pb-3 pr-3">Extensión</th>
                      <th scope="col" className="pb-3 pr-3">TTL (segundos)</th>
                      <th scope="col" className="pb-3 pr-3">Política de reemplazo</th>
                      <th scope="col" className="pb-3 text-right">Acción</th>
                    </tr>
                  </thead>
                  <tbody>
                    {fileTypes.map((type, index) => (
                      <tr key={type.rowId} className="border-t border-line">
                        <td className="py-3 pr-3">
                          <input name={`extension-${type.rowId}`} value={type.extension} onChange={(event) => updateFileType(type.rowId, 'extension', event.target.value)} aria-label={`Extensión ${index + 1}`} type="text" placeholder="png" required className="field-input font-mono" />
                        </td>
                        <td className="py-3 pr-3">
                          <input name={`ttl-${type.rowId}`} value={type.ttlSeconds} onChange={(event) => updateFileType(type.rowId, 'ttlSeconds', event.target.value)} aria-label={`TTL en segundos ${index + 1}`} type="number" min="0" step="1" required className="field-input" />
                        </td>
                        <td className="py-3 pr-3">
                          <select name={`policy-${type.rowId}`} value={type.replacementPolicy} onChange={(event) => updateFileType(type.rowId, 'replacementPolicy', event.target.value)} aria-label={`Política de reemplazo ${index + 1}`} className="field-input">
                            {replacementPolicies.map((policy) => <option key={policy} value={policy}>{policy}</option>)}
                          </select>
                        </td>
                        <td className="py-3 text-right">
                          <button type="button" onClick={() => { clearValidation(); setFileTypes(fileTypes.filter((item) => item.rowId !== type.rowId)); }} aria-label={`Quitar tipo de archivo ${index + 1}`} className="rounded px-2 py-2 text-red-700 hover:bg-red-50">Quitar</button>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </section>

          <fieldset className="rounded-xl border border-line bg-surface p-6">
            <legend className="px-2 text-lg font-semibold">Mecanismo de acceso</legend>
            <div className="grid gap-4 md:grid-cols-2">
              <label className="flex cursor-pointer items-center gap-3 rounded-lg border border-line p-4 has-checked:border-primary has-checked:bg-soft">
                <input name="auth-mode" type="radio" value="api-key" checked={authMode === 'api-key'} onChange={() => setAuthMode('api-key')} className="accent-primary" required />
                <span className="text-sm font-medium">API key (x-api-key)</span>
              </label>
              <label className="flex cursor-pointer items-center gap-3 rounded-lg border border-line p-4 has-checked:border-primary has-checked:bg-soft">
                <input name="auth-mode" type="radio" value="password" checked={authMode === 'password'} onChange={() => setAuthMode('password')} className="accent-primary" required />
                <span className="text-sm font-medium">Usuario y contraseña</span>
              </label>
            </div>
          </fieldset>
        </form>

        {rule ? (
          <RuleCredentials key={authMode} rule={rule} authMode={authMode} />
        ) : (
          <p className="rounded border border-line bg-soft p-4 text-sm text-muted">Guarda la regla para administrar sus claves o usuarios.</p>
        )}

        {message && <p role="status" className="form-message">{message}</p>}

        <div className="flex justify-end gap-3 border-t border-line pt-5">
          <a href={backUrl} className="rounded border border-line bg-surface px-4 py-2.5 text-sm font-medium hover:bg-soft">Cancelar</a>
          <button type="submit" form="rule-form" disabled={submitting} className="button-primary">{submitting ? 'Guardando…' : 'Guardar regla'}</button>
        </div>
      </div>
    </>
  );
}
