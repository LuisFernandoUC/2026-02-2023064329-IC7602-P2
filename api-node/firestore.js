import { getFirestore } from 'firebase-admin/firestore';
import { getFirebaseApp } from './firebase.js';

export function getDb() {
  return getFirestore(getFirebaseApp());
}
