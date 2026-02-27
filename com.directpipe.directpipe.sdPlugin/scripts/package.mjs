// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

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
