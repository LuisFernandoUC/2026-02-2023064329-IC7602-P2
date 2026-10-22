import { getDb } from './firestore.js';

let db;

try {
  db = getDb();
  await db.collection('domains').limit(1).get();
  console.log('Conexión con Firestore verificada.');
} catch (error) {
  console.error(`No se pudo verificar Firestore: ${error.message}`);
  process.exitCode = 1;
} finally {
  if (db) {
    await db.terminate();
  }
}
