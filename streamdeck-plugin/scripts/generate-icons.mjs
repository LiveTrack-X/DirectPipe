#!/usr/bin/env node
/**
 * Generate PNG icons from SVG sources for Stream Deck plugin.
 *
 * Icon size requirements (Elgato SDK v2):
 *   Plugin icon:   256x256 + 512x512 (@2x)
 *   Category icon:  28x28  +  56x56  (@2x)
 *   Action icons:   20x20  +  40x40  (@2x)
 *   State images:   72x72  + 144x144 (@2x)
 */

import sharp from 'sharp';
import { readFileSync, mkdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = join(__dirname, '..');
const SRC = join(ROOT, 'icons-src');
const OUT = join(ROOT, 'images');

mkdirSync(OUT, { recursive: true });

// Icon definitions: [svgName, outputName, standardSize]
const icons = [
  // Plugin icon (256 + 512@2x)
  { src: 'directpipe.svg', out: 'directpipe', size: 256, x2: 512 },

  // Category icon (28 + 56@2x)
  { src: 'category.svg', out: 'category', size: 28, x2: 56 },

  // Action icons (20 + 40@2x) — shown in action list
  { src: 'bypass.svg', out: 'bypass', size: 20, x2: 40 },
  { src: 'panic.svg', out: 'panic', size: 20, x2: 40 },
  { src: 'volume.svg', out: 'volume', size: 20, x2: 40 },
  { src: 'preset.svg', out: 'preset', size: 20, x2: 40 },
  { src: 'monitor.svg', out: 'monitor', size: 20, x2: 40 },

  // State images (72 + 144@2x) — shown on Stream Deck keys
  { src: 'bypass-active.svg', out: 'bypass-active', size: 72, x2: 144 },
  { src: 'bypass-off.svg', out: 'bypass-off', size: 72, x2: 144 },
  { src: 'panic-off.svg', out: 'panic-off', size: 72, x2: 144 },
  { src: 'panic-on.svg', out: 'panic-on', size: 72, x2: 144 },
  { src: 'volume-on.svg', out: 'volume-on', size: 72, x2: 144 },
  { src: 'volume-muted.svg', out: 'volume-muted', size: 72, x2: 144 },
  { src: 'preset-active.svg', out: 'preset-active', size: 72, x2: 144 },
  { src: 'monitor-on.svg', out: 'monitor-on', size: 72, x2: 144 },
  { src: 'monitor-off.svg', out: 'monitor-off', size: 72, x2: 144 },
];

async function generateIcon({ src, out, size, x2 }) {
  const svgPath = join(SRC, src);
  const svgBuffer = readFileSync(svgPath);

  // Standard size
  const stdPath = join(OUT, `${out}.png`);
  await sharp(svgBuffer, { density: 300 })
    .resize(size, size)
    .png()
    .toFile(stdPath);

  // @2x (high DPI)
  const x2Path = join(OUT, `${out}@2x.png`);
  await sharp(svgBuffer, { density: 300 })
    .resize(x2, x2)
    .png()
    .toFile(x2Path);

  console.log(`  ${out}: ${size}x${size} + ${x2}x${x2}@2x`);
}

async function main() {
  console.log('Generating Stream Deck icons...\n');
  console.log(`  Source: ${SRC}`);
  console.log(`  Output: ${OUT}\n`);

  for (const icon of icons) {
    await generateIcon(icon);
  }

  console.log(`\nDone! Generated ${icons.length * 2} PNG files.`);
}

main().catch((err) => {
  console.error('Error generating icons:', err);
  process.exit(1);
});
