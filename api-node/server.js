import app from './app.js';

const host = process.env.HOST;
const port = Number(process.env.PORT);

if (!host || !Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error('Configura HOST y PORT (1-65535) en .env.');
}

app.listen(port, host, () => {
  console.log(`API en http://${host}:${port}`);
});
