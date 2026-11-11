# Elgato utils compatibility patch

`elgato-utils` contains the complete MIT-licensed `@elgato/utils` 0.4.1 npm
package used by the existing Stream Deck SDK 2.0.1. The upstream changes are:

- `package.json` dependency `zod: ^3.25.24` to `zod: 4.6.7`.
- `dist/rpc/json-rpc/error.js`: explicitly optional `data: z.optional(z.any())`.
- `dist/rpc/json-rpc/response.js`: explicitly optional `result: z.optional(z.any())`.

The two explicit optional markers preserve the previous validator's acceptance
of absent/undefined any-valued fields under patched Zod. All other upstream
bytes, including public declarations, README and LICENSE, are unchanged.

The utils runtime already imports `zod/v4-mini`, which the patched Zod 4.6.7
preserves. This updates the actual validator rather than suppressing its
advisory or forcing an untested dependency override. Local RPC/schema and
bundled SDK tests verify compatibility; collection-bound tests exercise the
security fix. Remove this bridge when Elgato publishes a compatible dependency
declaration for patched Zod.

Source: https://registry.npmjs.org/@elgato/utils/-/utils-0.4.1.tgz

SHA-512 integrity:
`sha512-Az66McfjYXajL7ZLud+SaO0n2+Q2RedVwxfRdkS1d5uyNiXf25KWOfdU2zS7un3H/d+a+lxdX7pFqiKTDf6/UA==`

Advisory: https://github.com/advisories/GHSA-mrhx-c7xc-m3w5

The upstream MIT license is retained in `elgato-utils/LICENSE` and copied into
the packaged third-party notices by the build. `.npmrc` packs the local file
dependency as a regular package, avoiding upstream development-only tooling.
