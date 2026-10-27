import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
const simulatorProxy = {
  '/api': { target: 'http://127.0.0.1:8080', ws: true },
};
export default defineConfig({
  plugins: [react()],
  server: { proxy: simulatorProxy },
  preview: { proxy: simulatorProxy },
  test: { environment: 'jsdom', setupFiles: ['./src/test-setup.ts'] },
});
