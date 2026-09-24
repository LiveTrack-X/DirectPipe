// SPDX-License-Identifier: GPL-3.0-or-later
// Check generated action-list PNGs, not the colored plugin logo or key states.
// https://docs.elgato.com/guidelines/stream-deck/plugins/#icons
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';

// An optional root also allows checking files extracted from the final package.
const root = resolve(process.argv[2] ?? resolve(dirname(fileURLToPath(import.meta.url)), '..'));
const manifest = JSON.parse(readFileSync(resolve(root, 'manifest.json'), 'utf8'));
const references = [
  { name: 'Category', icon: manifest.CategoryIcon, size: 28 },
  ...manifest.Actions.map(action => ({ name: action.Name, icon: action.Icon, size: 20 })),
];

let checked = 0;
for (const { name, icon, size } of references) {
  assert.equal(typeof icon, 'string', `${name}: missing action-list icon`);
  for (const scale of [1, 2]) {
    const path = resolve(root, `${icon}${scale === 2 ? '@2x' : ''}.png`);
    const label = `${name} (${scale}x)`;
    const metadata = await sharp(path).metadata();
    assert.equal(metadata.format, 'png', `${label}: expected generated PNG`);
    assert.equal(metadata.width, size * scale, `${label}: incorrect width`);
    assert.equal(metadata.height, size * scale, `${label}: incorrect height`);
    assert.equal(metadata.hasAlpha, true, `${label}: missing transparency`);
    const { data, info } = await sharp(path).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    assert.equal(info.channels, 4, `${label}: expected RGBA`);
    let visible = 0;
    for (let i = 0; i < data.length; i += 4) {
      if (data[i + 3] === 0) continue; // RGB of fully transparent pixels is irrelevant.
      visible++;
      assert(data[i] === 255 && data[i + 1] === 255 && data[i + 2] === 255,
        `${label}: visible pixels must be #FFFFFF`);
    }
    assert(visible > 0, `${label}: empty icon`);
    for (const pixel of [0, info.width - 1, (info.height - 1) * info.width, info.width * info.height - 1]) {
      assert.equal(data[pixel * 4 + 3], 0, `${label}: canvas corners must be transparent`);
    }
    checked++;
  }
  console.log(`PASS ${name}: white transparent ${size}/${size * 2}px`);
}
console.log(`Verified ${checked} action/category PNG references. Plugin branding and key states are separate.`);
