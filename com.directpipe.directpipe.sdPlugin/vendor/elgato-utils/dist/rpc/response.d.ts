import type { JsonObject, JsonPrimitive, JsonValue } from "../json.js";
import type { JsonRpcError } from "./json-rpc/error.js";
import type { RpcSender } from "./sender.js";
/**
 * Response object sent from a server.
 */
export type RpcResponse<T extends RpcResponseResult = RpcResponseResult> = RpcErrorResponse | RpcSuccessResponse<T>;
/**
 * Success response object sent from a server.
 */
export type RpcSuccessResponse<T extends RpcResponseResult = RpcResponseResult> = {
    /**
     * Result of the request.
     */
    readonly result: T;
    /**
     * Determines whether the request was successful.
     */
    readonly ok: true;
};
/**
 * Error response object sent from a server.
 */
export type RpcErrorResponse = {
    /**
     * The error that occurred.
     */
    readonly error: JsonRpcError;
    /**
     * Determines whether the request was successful.
     */
    readonly ok: false;
};
/**
 * Type of a result contained within a response.
 */
export type RpcResponseResult = Exclude<JsonPrimitive, undefined> | JsonObject | JsonValue[];
/**
 * Responder responsible for responding to a request.
 */
export declare class RpcRequestResponder {
    #private;
    /**
     * Initializes a new instance of the {@link RpcRequestResponder} class.
     * @param send Function responsible for sending responses.
     * @param id Identifier of the request.
     */
    constructor(send: RpcSender, id: string | undefined);
    /**
     * Indicates whether a response can be sent.
     * @returns `true` when a response has not yet been set and the request accepts responses.
     */
    get canRespond(): boolean;
    /**
     * Sends an error response to the client.
     * @param error The error.
     * @returns Promise fulfilled once the response has been sent.
     */
    error(error: JsonRpcError): Promise<void>;
    /**
     * Sends the result to the client.
     * @param result The result.
     * @returns Promise fulfilled once the response has been sent.
     */
    success(result: RpcResponseResult): Promise<void>;
}
