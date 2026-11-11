import { z } from "zod/v4-mini";
/**
 * Request object sent to a server.
 */
export const JsonRpcRequest = z.object({
    jsonrpc: z.literal("2.0"),
    method: z.string(),
    params: z.optional(z.union([
        z.record(z.string(), z.any()),
        z.array(z.any()),
    ])),
    id: z.optional(z.string()),
});
