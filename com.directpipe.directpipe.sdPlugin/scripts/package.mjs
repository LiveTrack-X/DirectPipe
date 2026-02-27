#!/usr/bin/env node
/**
 * Package Stream Deck plugin as .streamDeckPlugin file.
 * Creates a ZIP with correct folder structure: com.directpipe.directpipe.sdPlugin/
 */

import { execSync } from "child_process";
import { cpSync, mkdirSync, rmSync, renameSync, statSync, existsSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = join(__dirname, "..");
const DIST = join(ROOT, "..", "dist");
const UUID = "com.directpipe.directpipe";
const TMP = join(DIST, "_tmp_sd");
const SD_PLUGIN_DIR = join(TMP, `${UUID}.sdPlugin`);
const OUTPUT = join(DIST, `${UUID}.streamDeckPlugin`);

// Clean
if (existsSync(TMP)) rmSync(TMP, { recursive: true, force: true });
if (existsSync(OUTPUT)) rmSync(OUTPUT, { force: true });

// Create sdPlugin folder
mkdirSync(SD_PLUGIN_DIR, { recursive: true });

// Copy manifest.json
cpSync(join(ROOT, "manifest.json"), join(SD_PLUGIN_DIR, "manifest.json"));

// Copy bin/
cpSync(join(ROOT, "bin"), join(SD_PLUGIN_DIR, "bin"), { recursive: true });

// Copy images/
cpSync(join(ROOT, "images"), join(SD_PLUGIN_DIR, "images"), { recursive: true });

// Copy Property Inspector HTML files
mkdirSync(join(SD_PLUGIN_DIR, "src", "inspectors"), { recursive: true });
cpSync(join(ROOT, "src", "inspectors"), join(SD_PLUGIN_DIR, "src", "inspectors"), { recursive: true });

// Create ZIP using PowerShell Compress-Archive
const zipPath = OUTPUT.replace(".streamDeckPlugin", ".zip");
execSync(
  `powershell.exe -NoProfile -Command "Compress-Archive -Path '${TMP.replace(/\//g, "\\")}\\*' -DestinationPath '${zipPath.replace(/\//g, "\\")}' -Force"`,
  { stdio: "inherit" }
);

// Rename .zip to .streamDeckPlugin
renameSync(zipPath, OUTPUT);

// Cleanup
rmSync(TMP, { recursive: true, force: true });

// Report
const size = statSync(OUTPUT).size;
console.log(`\nPackaged: ${UUID}.streamDeckPlugin (${size} bytes)`);
