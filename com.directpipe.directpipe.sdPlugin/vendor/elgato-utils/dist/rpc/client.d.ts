import type { JsonValue } from "../json.js";
import { type RpcRequestOptions, type RpcRequestParameters } from "./request.js";
import { type RpcErrorResponse, type RpcResponse, type RpcResponseResult } from "./response.js";
import type { RpcSender } from "./sender.js";
/**
 * Client capable of sending requests to, and receiving responses from, a server.
 */
export declare class RpcClient {
    #private;
    /**
     * Initializes a new instance of the {@link RpcClient} class.
     * @param send Function responsible for sending requests.
     * @param options Client options.
     */
    constructor(send: RpcSender, options?: RpcClientOptions);
    /**
     * Sends a notification to the RPC server.
     * @param request The request.
     */
    notify(request: RpcRequestOptions): Promise<void>;
    /**
     * Sends a notification to the RPC server.
     * @param method Name of the method to invoke.
     * @param params Parameters to be used during the invocation of the method.
     */
    notify(method: string, params?: RpcRequestParameters): Promise<void>;
    /**
     * Attempts to process the specified value as a response from a server.
     * @param value Value to process.
     * @returns `true` when the client was able to process the value as a response; otherwise `false`.
     */
    receive(value: JsonValue): Promise<boolean>;
    /**
     * Sends the request to the RPC server.
     * @param request The request.
     * @returns The response.
     */
    request<TResult extends RpcResponseResult>(request: RpcRequestOptions): Promise<RpcResponse<TResult>>;
    /**
     * Sends the request to the RPC server.
     * @param method Name of the method to invoke.
     * @param params Parameters to be used during the invocation of the method.
     * @returns The response.
     */
    request<TResult extends RpcResponseResult>(method: string, params?: RpcRequestParameters): Promise<RpcResponse<TResult>>;
}
/**
 * Options for an RPC client.
 */
export type RpcClientOptions = {
    /**
     * Function invoked when an error response was received that was not associated with a request.
     * @param response The response.
     */
    error?: (response: RpcErrorResponse) => void;
};
