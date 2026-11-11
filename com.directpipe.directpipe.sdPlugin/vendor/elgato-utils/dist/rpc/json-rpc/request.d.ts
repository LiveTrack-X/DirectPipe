import { z } from "zod/v4-mini";
import type { JsonObject, JsonValue } from "../../json.js";
/**
 * Request object sent to a server.
 */
export type JsonRpcRequest<T extends JsonObject | JsonValue[] | undefined = JsonObject | JsonValue[] | undefined> = {
    /**
     * The JSON-RPC version.
     */
    readonly jsonrpc: "2.0";
    /**
     * Name of the method to be invoked.
     */
    readonly method: string;
    /**
     * Optional parameters to send with the RPC request.
     */
    readonly params?: T;
    /**
     * An identifier established by the client used to identify the response; when undefined, the request
     * is treated as a notification.
     */
    readonly id?: string;
};
/**
 * Request object sent to a server.
 */
export declare const JsonRpcRequest: z.ZodMiniType<JsonRpcRequest>;
