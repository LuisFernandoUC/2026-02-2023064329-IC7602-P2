import { useState } from 'react';
import { Brand } from '../components/Layout.jsx';
import AddDomainDialog from '../components/AddDomainDialog.jsx';

const exampleDomains = [
  { id: 'example', name: 'example.com', verified: true, cacheSize: '500 MB' },
  { id: 'static', name: 'static.example.net', verified: false, cacheSize: '250 MB' },
];

export default function Domains() {
  const [addingDomain, setAddingDomain] = useState(false);

  return (
    <div className="min-h-screen">
      <header className="border-b border-line bg-surface">
        <div className="mx-auto flex max-w-7xl flex-wrap items-center justify-between gap-5 px-6 py-5">
          <div className="flex items-center gap-8">
            <Brand href="#/dominios" />
            <nav aria-label="Principal">
              <a href="#/dominios" aria-current="page" className="text-link font-medium">Dominios</a>
            </nav>
          </div>
          <div className="flex items-center gap-6 text-sm text-muted">
            <span>cuenta@ejemplo.com</span>
            <button type="button" disabled title="Disponible cuando se conecte el inicio de sesión" className="cursor-not-allowed opacity-50">Cerrar sesión</button>
          </div>
        </div>
      </header>

      <main className="mx-auto max-w-7xl px-6 py-10">
        <div className="flex flex-wrap items-center justify-between gap-5 border-b border-line pb-7">
          <div>
            <h1 className="text-3xl font-semibold tracking-tight">Mis dominios</h1>
            <p className="mt-3 text-muted">Administra tus dominios y sus reglas de caché.</p>
          </div>
          <button type="button" onClick={() => setAddingDomain(true)} className="button-primary">
            <span aria-hidden="true" className="text-xl leading-none">+</span> Agregar dominio
          </button>
        </div>

        <p className="my-5 text-sm text-muted">Vista con datos de ejemplo. Todavía no se cargan los dominios de tu cuenta.</p>

        <div className="overflow-x-auto rounded-lg border border-line bg-surface">
          <table className="w-full text-left text-sm">
            <caption className="sr-only">Dominios de ejemplo y su estado de verificación</caption>
            <thead className="bg-soft text-xs uppercase tracking-wide text-muted">
              <tr>
                <th scope="col" className="px-5 py-4">Dominio</th>
                <th scope="col" className="px-5 py-4">Estado de verificación</th>
                <th scope="col" className="px-5 py-4">Límite de caché</th>
                <th scope="col" className="px-5 py-4 text-right">Acciones</th>
              </tr>
            </thead>
            <tbody>
              {exampleDomains.map((domain) => (
                <tr key={domain.id} className="border-t border-line">
                  <th scope="row" className="px-5 py-5 font-mono font-semibold">{domain.name}</th>
                  <td className="px-5 py-5">
                    <span className={`inline-flex items-center gap-1.5 whitespace-nowrap rounded-full border px-2.5 py-1 text-xs font-medium ${domain.verified ? 'border-emerald-200 bg-emerald-50 text-emerald-800' : 'border-amber-200 bg-amber-50 text-amber-800'}`}>
                      <span aria-hidden="true">{domain.verified ? '✓' : '◷'}</span>
                      {domain.verified ? 'Verificado' : 'Pendiente'}
                    </span>
                  </td>
                  <td className="whitespace-nowrap px-5 py-5 font-mono text-muted">{domain.cacheSize}</td>
                  <td className="px-5 py-5">
                    <div className="flex justify-end gap-4 whitespace-nowrap">
                      {!domain.verified && <button type="button" disabled title="Disponible en el flujo de verificación TXT" className="cursor-not-allowed text-primary opacity-50">Verificar</button>}
                      <button type="button" disabled title="Disponible en el detalle del dominio" className="cursor-not-allowed text-primary opacity-50">Abrir</button>
                      <button type="button" disabled title="Disponible al conectar la administración de dominios" className="cursor-not-allowed text-red-700 opacity-50">Eliminar</button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </main>
      {addingDomain && <AddDomainDialog onClose={() => setAddingDomain(false)} />}
    </div>
  );
}
