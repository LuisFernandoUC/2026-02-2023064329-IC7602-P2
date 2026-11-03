export function Brand({ href = '#/iniciar-sesion' }) {
  return (
    <a href={href} className="inline-flex items-center gap-3 rounded font-semibold tracking-tight">
      <span className="flex h-9 w-9 items-center justify-center rounded bg-primary text-white">
        <svg width="23" height="23" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.7" aria-hidden="true">
          <path d="M3 3h18v7H3zM3 14h18v7H3z" />
          <path d="M6 6.5h2M6 17.5h2" />
        </svg>
      </span>
      <span className="text-xl">Proxy cache</span>
    </a>
  );
}

export function AuthLayout({ title, description, children, footer, brandHref = '#/iniciar-sesion' }) {
  return (
    <div className="flex min-h-screen flex-col">
      <header className="px-6 py-5 lg:px-10">
        <Brand href={brandHref} />
      </header>
      <main className="flex flex-1 items-center justify-center px-6 py-10">
        <section className="w-full max-w-md rounded-xl bg-surface p-8 shadow-sm" aria-labelledby="page-title">
          <h1 id="page-title" className="text-3xl font-semibold tracking-tight">{title}</h1>
          <p className="mt-3 text-sm leading-6 text-muted">{description}</p>
          <div className="mt-7">{children}</div>
          <p className="mt-6 rounded bg-soft px-4 py-3 text-center text-sm text-muted">{footer}</p>
        </section>
      </main>
    </div>
  );
}
