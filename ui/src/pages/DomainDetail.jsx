export default function DomainDetail({ domain }) {
  if (!domain) {
    return (
      <>
        <a href="#/dominios" className="text-link text-sm">← Volver a Mis dominios</a>
        <h1 className="mt-6 text-3xl font-semibold">Dominio no encontrado</h1>
        <p className="mt-4 text-muted">Este dominio no está disponible en la vista de ejemplo.</p>
      </>
    );
  }

  const rules = domain.rules ?? [];

  return (
    <>
      <a href="#/dominios" className="text-link text-sm">← Volver a Mis dominios</a>

      <div className="mt-6 flex flex-wrap items-center justify-between gap-6 rounded-xl border border-line bg-surface p-6">
        <div>
          <div className="flex flex-wrap items-center gap-3">
            <h1 className="text-3xl font-semibold tracking-tight">{domain.name}</h1>
            <span className={`rounded-full border px-2.5 py-1 text-xs font-medium ${domain.verified ? 'border-emerald-200 bg-emerald-50 text-emerald-800' : 'border-amber-200 bg-amber-50 text-amber-800'}`}>
              {domain.verified ? 'Verificado' : 'Pendiente'}
            </span>
          </div>
          {!domain.verified && <a href={`#/dominios/${domain.id}/verificar`} className="text-link mt-3 inline-block text-sm">Verificar dominio</a>}
        </div>
        <dl className="rounded-lg bg-soft px-5 py-4">
          <dt className="text-xs font-medium uppercase tracking-wide text-muted">Límite total de caché</dt>
          <dd className="mt-2 font-mono text-2xl font-semibold">{domain.cacheSize}</dd>
        </dl>
      </div>

      <p className="my-5 text-sm text-muted">Vista con datos de ejemplo. Todavía no se cargan las reglas de tu cuenta.</p>

      <section aria-labelledby="rules-title">
        <div className="mb-5 flex flex-wrap items-center justify-between gap-4">
          <div className="flex flex-wrap items-center gap-3">
            <h2 id="rules-title" className="text-xl font-semibold">Reglas de URL</h2>
            <span className="rounded-full bg-soft px-3 py-1 text-xs text-muted">{rules.length} {rules.length === 1 ? 'regla configurada' : 'reglas configuradas'}</span>
          </div>
          <button type="button" disabled title="Disponible en el formulario de reglas" className="button-primary">
            <span aria-hidden="true" className="text-xl leading-none">+</span> Agregar regla
          </button>
        </div>

        {rules.length === 0 ? (
          <div className="rounded-xl border border-line bg-surface px-6 py-12 text-center">
            <h3 className="text-lg font-semibold">Todavía no tienes reglas</h3>
            <p className="mt-3 text-sm text-muted">Agrega una regla para configurar las rutas y el acceso a los archivos.</p>
          </div>
        ) : (
          <div className="overflow-x-auto rounded-lg border border-line bg-surface">
            <table className="w-full text-left text-sm">
              <caption className="sr-only">Reglas de URL de ejemplo para {domain.name}</caption>
              <thead className="bg-soft text-xs uppercase tracking-wide text-muted">
                <tr>
                  <th scope="col" className="px-5 py-4">Ruta o patrón</th>
                  <th scope="col" className="px-5 py-4">Tamaño máx. de caché</th>
                  <th scope="col" className="px-5 py-4">Tipos de archivo</th>
                  <th scope="col" className="px-5 py-4">Mecanismo de acceso</th>
                  <th scope="col" className="px-5 py-4 text-right">Acciones</th>
                </tr>
              </thead>
              <tbody>
                {rules.map((rule) => (
                  <tr key={rule.id} className="border-t border-line">
                    <th scope="row" className="whitespace-nowrap px-5 py-5 font-mono font-medium">{rule.pathPattern}</th>
                    <td className="whitespace-nowrap px-5 py-5 font-mono text-muted">{rule.cacheSize}</td>
                    <td className="px-5 py-5">
                      <ul className="flex flex-wrap gap-1.5" aria-label="Tipos de archivo">
                        {rule.fileTypes.map((extension) => <li key={extension} className="rounded bg-soft px-2 py-1 font-mono text-xs">{extension}</li>)}
                      </ul>
                    </td>
                    <td className="whitespace-nowrap px-5 py-5">{rule.authMode === 'api-key' ? 'API key (x-api-key)' : 'Usuario y contraseña'}</td>
                    <td className="px-5 py-5">
                      <div className="flex justify-end gap-4">
                        <button type="button" disabled title="Disponible en el formulario de reglas" aria-label={`Editar regla ${rule.pathPattern}`} className="cursor-not-allowed text-primary opacity-50">Editar</button>
                        <button type="button" disabled title="Disponible al conectar la administración de reglas" aria-label={`Eliminar regla ${rule.pathPattern}`} className="cursor-not-allowed text-red-700 opacity-50">Eliminar</button>
                      </div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>
    </>
  );
}
