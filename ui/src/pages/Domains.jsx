import { useEffect, useState } from 'react';
import { signOut } from 'firebase/auth';
import { getPanelAuth } from '../auth.js';
import { apiRequest } from '../api.js';
import { Brand } from '../components/Layout.jsx';
import AddDomainDialog from '../components/AddDomainDialog.jsx';
import DeleteDialog from '../components/DeleteDialog.jsx';
import VerifyDomain from './VerifyDomain.jsx';
import DomainDetail from './DomainDetail.jsx';
import RuleForm from './RuleForm.jsx';

function domainView(domain) {
  return { ...domain, cacheSize: `${domain.maxCacheBytes / (1024 * 1024)} MB` };
}

export default function Domains({ user, verificationId, domainId, ruleForm }) {
  const [domains, setDomains] = useState([]);
  const [loading, setLoading] = useState(true);
  const [loadError, setLoadError] = useState('');
  const [addingDomain, setAddingDomain] = useState(false);
  const [deletingDomain, setDeletingDomain] = useState(null);
  const [removingDomain, setRemovingDomain] = useState(false);
  const [message, setMessage] = useState('');
  const [signingOut, setSigningOut] = useState(false);

  useEffect(() => {
    let active = true;
    apiRequest('/api/domains')
      .then((data) => { if (active) setDomains(data.map(domainView)); })
      .catch((error) => { if (active) setLoadError(error.message); })
      .finally(() => { if (active) setLoading(false); });
    return () => { active = false; };
  }, [user.uid]);

  function handleCreated(domain) {
    setDomains((current) => [...current, domainView(domain)]);
    setAddingDomain(false);
    setMessage('Dominio guardado. Falta verificar su propiedad.');
  }

  function handleVerified(domain) {
    setDomains((current) => current.map((item) => item.id === domain.id ? domainView(domain) : item));
  }

  async function deleteDomain(domain) {
    setRemovingDomain(true);
    setMessage('');
    try {
      await apiRequest(`/api/domains/${encodeURIComponent(domain.id)}`, { method: 'DELETE' });
      setDomains((current) => current.filter((item) => item.id !== domain.id));
      setMessage('Dominio eliminado.');
    } catch (error) {
      setMessage(error.message);
    } finally {
      setRemovingDomain(false);
    }
  }

  async function handleLogout() {
    setSigningOut(true);
    try {
      await signOut(getPanelAuth());
    } catch {
      setMessage('No se pudo cerrar la sesión. Intenta de nuevo.');
    } finally {
      setSigningOut(false);
    }
  }

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
            <span>{user.email}</span>
            <button type="button" onClick={handleLogout} disabled={signingOut} className="rounded hover:text-primary disabled:opacity-50">{signingOut ? 'Cerrando sesión…' : 'Cerrar sesión'}</button>
          </div>
        </div>
      </header>

      <main className="mx-auto max-w-7xl px-6 py-10">
        {message && <p role="status" className="form-message mb-5">{message}</p>}
        {loading ? (
          <p role="status" className="text-muted">Cargando dominios…</p>
        ) : loadError ? (
          <p role="alert" className="form-message">{loadError}</p>
        ) : verificationId ? (
          <VerifyDomain key={verificationId} domain={domains.find((domain) => domain.id === verificationId)} onVerified={handleVerified} />
        ) : ruleForm ? (
          <RuleForm key={`${ruleForm.domainId}/${ruleForm.ruleId ?? 'new'}`} domain={domains.find((domain) => domain.id === ruleForm.domainId)} ruleId={ruleForm.ruleId} />
        ) : domainId ? (
          <DomainDetail key={domainId} domain={domains.find((domain) => domain.id === domainId)} />
        ) : (
          <>
            <div className="flex flex-wrap items-center justify-between gap-5 border-b border-line pb-7">
              <div>
                <h1 className="text-3xl font-semibold tracking-tight">Mis dominios</h1>
                <p className="mt-3 text-muted">Administra tus dominios y sus reglas de caché.</p>
              </div>
              <button type="button" onClick={() => setAddingDomain(true)} className="button-primary">
                <span aria-hidden="true" className="text-xl leading-none">+</span> Agregar dominio
              </button>
            </div>

            {domains.length === 0 ? (
              <div className="mt-7 rounded-lg border border-line bg-surface px-6 py-12 text-center">
                <h2 className="text-lg font-semibold">Todavía no tienes dominios</h2>
                <p className="mt-3 text-sm text-muted">Agrega un dominio para configurar su caché.</p>
              </div>
            ) : (
              <div className="mt-7 overflow-x-auto rounded-lg border border-line bg-surface">
                <table className="w-full text-left text-sm">
                  <caption className="sr-only">Tus dominios y su estado de verificación</caption>
                  <thead className="bg-soft text-xs uppercase tracking-wide text-muted">
                    <tr>
                      <th scope="col" className="px-5 py-4">Dominio</th>
                      <th scope="col" className="px-5 py-4">Estado de verificación</th>
                      <th scope="col" className="px-5 py-4">Límite de caché</th>
                      <th scope="col" className="px-5 py-4 text-right">Acciones</th>
                    </tr>
                  </thead>
                  <tbody>
                    {domains.map((domain) => (
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
                            {!domain.verified && <a href={`#/dominios/${domain.id}/verificar`} className="text-link">Verificar</a>}
                            <a href={`#/dominios/${domain.id}`} className="text-link" aria-label={`Abrir ${domain.name}`}>Abrir</a>
                            <button type="button" disabled={removingDomain} onClick={() => { setDeletingDomain(domain); setMessage(''); }} aria-label={`Eliminar dominio ${domain.name}`} className="rounded text-red-700 hover:underline disabled:opacity-50">Eliminar</button>
                          </div>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </>
        )}
      </main>
      {addingDomain && <AddDomainDialog onClose={() => setAddingDomain(false)} onCreated={handleCreated} />}
      {deletingDomain && (
        <DeleteDialog
          title="¿Eliminar dominio?"
          name={deletingDomain.name}
          description="Se eliminarán también sus reglas, usuarios y claves."
          confirmLabel="Eliminar dominio"
          onClose={() => setDeletingDomain(null)}
          onConfirm={() => deleteDomain(deletingDomain)}
        />
      )}
    </div>
  );
}
