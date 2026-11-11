import { type IDisposable } from "../explicit-resource-management/disposable.js";
import type { JsonValue } from "../json.js";
import type { RpcRequestParameters } from "./request.js";
import { RpcRequestResponder, type RpcResponseResult } from "./response.js";
import type { RpcSender } from "./sender.js";
/**
 * Server capable of receiving requests from, and sending responses to, a client.
 */
export declare class RpcServer {
    #private;
    /**
     * Initializes a new instance of the {@link RpcServer} class.
     * @param send Function responsible for sending responses.
     */
    constructor(send: RpcSender);
    /**
     * Adds a method handler to this instance, allowing it to receive requests and notifications.
     * @param name The name of the method.
     * @param handler The function to be invoked when the method is called.
     * @returns A disposable object that can be used to remove the method handler.
     */
    addMethod<TParameters extends RpcRequestParameters>(name: string, handler: MethodHandler<TParameters>): IDisposable;
    /**
     * Attempts to process the specified request received from a client.
     * @param value Value to process.
     * @returns `true` when the server was able to process the request; otherwise `false`.
     */
    receive(value: JsonValue): Promise<boolean>;
}
/**
 * Function responsible for handling a request, and providing a result.
 * @template TParameters Type of the parameter sent with the request.
 */
export type MethodHandler<TParameters extends RpcRequestParameters = undefined> = (parameters: TParameters, responder: RpcRequestResponder, next: () => Promise<RpcResponseResult | void> | RpcResponseResult | void) => Promise<RpcResponseResult | void> | RpcResponseResult | void;
