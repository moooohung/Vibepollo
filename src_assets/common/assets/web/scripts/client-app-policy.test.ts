import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import ts from 'typescript';

// Exercise the actual editor normalization, concurrent merge and save payload.
const component = readFileSync(new URL('../views/DevicesView.vue', import.meta.url), 'utf8');
const script = component.split('<script setup lang="ts">')[1]!.split('</script>')[0]!;
const source = ts.createSourceFile('DevicesView.ts', script, ts.ScriptTarget.Latest, true);
const declarations = source.statements
  .filter(
    (statement) =>
      ts.isFunctionDeclaration(statement) ||
      (ts.isVariableStatement(statement) &&
        statement.declarationList.declarations.some(
          (declaration) =>
            ts.isIdentifier(declaration.name) &&
            ['draftScalarKeys', 'PERMISSION_ALL'].includes(declaration.name.text),
        )),
  )
  .map((statement) => statement.getText(source))
  .join('\n');
const context = vm.createContext({ structuredClone, toRaw: (value: unknown) => value });
vm.runInContext(
  ts.transpileModule(declarations, { compilerOptions: { target: ts.ScriptTarget.ES2022 } })
    .outputText,
  context,
);
const call = (name: string, ...args: unknown[]) => context[name](...args);
const device = (policy?: unknown) =>
  call(
    'normalizeDevice',
    { uuid: 'paired', name: 'Client', perm: 0x071f1f00, allowed_app_uuids: policy },
    0,
  );
const guest = '11111111-1111-4111-8111-111111111111';

test('missing policy is unrestricted; malformed policy does not become unrestricted', () => {
  for (const value of [undefined, null]) assert.equal(device(value).allowed_app_uuids, null);
  for (const value of [false, 'null', 42, {}, [guest, 1]]) {
    assert.equal(JSON.stringify(device(value).allowed_app_uuids), '[]');
  }
});

test('save distinguishes deny all, selected applications and unrestricted access', () => {
  for (const value of [null, [], [guest]]) {
    const stored = device(value);
    const draft = call('draftFromDevice', stored);
    assert.equal(
      JSON.stringify(call('updatePayload', stored, draft).allowed_app_uuids),
      JSON.stringify(value),
    );
  }
});

test('unrelated edits preserve a concurrent application restriction', () => {
  const origin = call('draftFromDevice', device(null));
  const local = call('cloneDraft', origin);
  local.name = 'Renamed locally';
  const latest = call('draftFromDevice', device([guest]));
  const merged = call('mergeLatestDraft', origin, local, latest);
  assert.equal(merged.conflict, false);
  assert.equal(merged.draft.name, local.name);
  assert.equal(JSON.stringify(merged.draft.allowedAppUuids), JSON.stringify([guest]));
});

test('conflicting application restrictions require user reconciliation', () => {
  const origin = call('draftFromDevice', device(null));
  const local = call('draftFromDevice', device([]));
  const latest = call('draftFromDevice', device([guest]));
  assert.equal(call('mergeLatestDraft', origin, local, latest).conflict, true);
});
