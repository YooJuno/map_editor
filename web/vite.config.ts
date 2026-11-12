import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
export default defineConfig({
  plugins: [react()],
  build: {
    target: 'es2022',
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (id.includes('/@firebase/firestore/')) return 'firebase-firestore';
          if (id.includes('/@firebase/auth/')) return 'firebase-auth';
          if (/node_modules\/(?:@firebase|firebase)\//.test(id)) return 'firebase-core';
        },
      },
    },
  },
  test: {
    fileParallelism: false,
    include: ['tests/**/*.test.{ts,tsx}'],
    exclude: process.env.FIRESTORE_EMULATOR_HOST
      ? []
      : ['tests/rules.test.ts', 'tests/firebase.test.ts', 'tests/workspaces.test.ts'],
  },
});
