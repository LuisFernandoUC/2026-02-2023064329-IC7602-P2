import express from 'express';
import cors from 'cors';

const app = express();

app.use(cors({ origin: process.env.UI_ORIGIN || false }));

app.get('/api/health', (req, res) => {
  res.json({ status: 'ok' });
});

app.use((req, res) => {
  res.status(404).json({ error: 'Ruta no encontrada' });
});

export default app;
