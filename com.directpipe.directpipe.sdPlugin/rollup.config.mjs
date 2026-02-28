import resolve from "@rollup/plugin-node-resolve";
import commonjs from "@rollup/plugin-commonjs";

export default {
  input: "src/plugin.js",
  output: {
    file: "bin/plugin.js",
    format: "cjs",
    sourcemap: false,
  },
  plugins: [
    resolve({ preferBuiltins: true }),
    commonjs(),
  ],
  // Node.js built-ins should not be bundled
  external: [
    "events",
    "http",
    "https",
    "net",
    "tls",
    "url",
    "util",
    "stream",
    "zlib",
    "os",
    "path",
    "fs",
    "crypto",
    "buffer",
    "child_process",
    "assert",
    "querystring",
    "string_decoder",
    "dgram",
  ],
};
