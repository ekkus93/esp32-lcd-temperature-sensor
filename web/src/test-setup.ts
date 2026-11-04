import '@testing-library/jest-dom/vitest';
import { cleanup, configure } from '@testing-library/react';
import { afterEach } from 'vitest';
// Lazy-loaded routes can take longer than the 1 s default to resolve on a busy
// machine, which made findBy* queries fail intermittently.
configure({ asyncUtilTimeout: 5000 });
afterEach(cleanup);
