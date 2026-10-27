import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../..', import.meta.url));
const generator = path.join(root, 'web/node_modules/.bin/json2ts');
const formatter = path.join(root, 'web/node_modules/.bin/prettier');
const files = [
  ['snapshot.schema.json', 'generated.ts'],
  ['auth-state.schema.json', 'api-auth.generated.ts'],
  ['alerts.schema.json', 'api-alerts.generated.ts'],
  ['history.schema.json', 'api-history.generated.ts'],
  ['sensor-configurations.schema.json', 'api-sensors.generated.ts'],
  ['sensor-patch.schema.json', 'api-sensor-patch.generated.ts'],
  ['device-settings.schema.json', 'api-device-settings.generated.ts'],
  [
    'device-settings-patch.schema.json',
    'api-device-settings-patch.generated.ts',
  ],
  ['wifi-status.schema.json', 'api-wifi-status.generated.ts'],
  ['wifi-scan.schema.json', 'api-wifi-scan.generated.ts'],
];

for (const [schema, types] of files) {
  const output = path.join(root, 'web/src/api', types);
  execFileSync(
    generator,
    ['-i', path.join(root, 'contracts', schema), '-o', output, '--maxItems=0'],
    { stdio: 'inherit' },
  );
  if (types !== 'generated.ts') {
    const generated = readFileSync(output, 'utf8');
    writeFileSync(output, generated.replace(/^\/\* eslint-disable \*\/\n/, ''));
  }
}

execFileSync(
  formatter,
  [
    '--write',
    ...files.map(([, types]) => path.join(root, 'web/src/api', types)),
  ],
  { stdio: 'inherit' },
);
