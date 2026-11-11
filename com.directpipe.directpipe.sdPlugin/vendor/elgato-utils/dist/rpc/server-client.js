import {} from "../json.js";
import { RpcClient } from "./client.js";
import { RpcServer } from "./server.js";
/**
 * Create an RPC server-and-client, capable of receiving and sending requests and notifications.
 * @param send Function responsible for sending requests and responses.
 * @param options the options.
 * @returns The RPC server-and-client.
 */
export function createRpcServerClient(send, options) {
    const client = new RpcClient(send, options);
    const server = new RpcServer(send);
    return {
        addMethod: server.addMethod.bind(server),
        notify: client.notify.bind(client),
        request: client.request.bind(client),
        async receive(value) {
            return (await client.receive(value)) || (await server.receive(value));
        },
    };
}
