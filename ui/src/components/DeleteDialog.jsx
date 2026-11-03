import { useEffect, useRef } from 'react';

export default function DeleteDialog({ title, name, description, confirmLabel, onClose, onConfirm }) {
  const dialogRef = useRef(null);

  useEffect(() => {
    dialogRef.current.showModal();
  }, []);

  function closeDialog() {
    dialogRef.current.close();
  }

  return (
    <dialog ref={dialogRef} onClose={onClose} aria-labelledby="delete-title" aria-describedby="delete-description" className="m-auto w-[calc(100%-3rem)] max-w-md rounded-xl bg-surface p-6 text-ink shadow-xl backdrop:bg-ink/40">
      <h2 id="delete-title" className="text-xl font-semibold">{title}</h2>
      <p className="mt-4 break-all font-mono text-sm">{name}</p>
      <p id="delete-description" className="mt-3 text-sm leading-6 text-muted">{description}</p>
      <div className="mt-6 flex justify-end gap-3">
        <button type="button" onClick={closeDialog} autoFocus className="rounded border border-line px-4 py-2.5 text-sm font-medium hover:bg-soft">Cancelar</button>
        <button type="button" onClick={() => { onConfirm(); closeDialog(); }} className="rounded bg-red-700 px-4 py-2.5 text-sm font-semibold text-white hover:bg-red-800">{confirmLabel}</button>
      </div>
    </dialog>
  );
}
