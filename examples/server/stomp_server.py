#!/usr/bin/env python3
"""
Simple STOMP-over-WebSocket test server mimicking Spring Boot STOMP broker.
Zero external dependencies beyond the standard library or optional websockets package.
Uses standard asyncio / websockets or built-in asyncio server.
"""

import asyncio
import json
import logging
import sys
from typing import Dict, Set

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("stomp_server")


class StompProtocolHandler:
    def __init__(self, send_raw_fn):
        self.send_raw = send_raw_fn
        self.subscriptions: Dict[str, str] = {}  # sub_id -> destination
        self.session_id = "sess-001"
        self.connected = False

    async def handle_raw_data(self, data: str):
        # STOMP frames end with \x00 (NULL byte). Frames may be preceded/followed by \n (heartbeats).
        frames = data.split("\x00")
        for raw_frame in frames:
            frame_trimmed = raw_frame.strip()
            if not frame_trimmed:
                if "\n" in raw_frame or "\r" in raw_frame:
                    logger.debug("Received heartbeat \\n")
                continue

            await self._process_frame(frame_trimmed)

    async def _process_frame(self, frame_str: str):
        lines = frame_str.split("\n")
        command = lines[0].strip()
        headers = {}
        body = ""

        idx = 1
        while idx < len(lines):
            line = lines[idx]
            if line == "" or line == "\r":
                # Header-Body separator found
                body = "\n".join(lines[idx + 1 :])
                break
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip()] = v.strip()
            idx += 1

        logger.info(f"Received STOMP command: {command}, headers: {headers}, body: {body}")

        if command in ("CONNECT", "STOMP"):
            await self._handle_connect(headers)
        elif command == "SUBSCRIBE":
            await self._handle_subscribe(headers)
        elif command == "UNSUBSCRIBE":
            await self._handle_unsubscribe(headers)
        elif command == "SEND":
            await self._handle_send(headers, body)
        elif command == "DISCONNECT":
            await self._handle_disconnect(headers)
        else:
            logger.warning(f"Unknown STOMP command: {command}")

    async def _handle_connect(self, headers: dict):
        self.connected = True
        logger.info(f"Client connected with version {headers.get('accept-version', '1.0')}")
        response = (
            "CONNECTED\n"
            "version:1.2\n"
            f"session:{self.session_id}\n"
            "server:Python-Stomp-Simulator/1.0\n"
            "heart-beat:10000,10000\n"
            "\n"
            "\x00"
        )
        await self.send_raw(response)

    async def _handle_subscribe(self, headers: dict):
        sub_id = headers.get("id", "0")
        dest = headers.get("destination", "")
        self.subscriptions[sub_id] = dest
        logger.info(f"Subscription registered: id={sub_id}, destination={dest}")

    async def _handle_unsubscribe(self, headers: dict):
        sub_id = headers.get("id", "")
        if sub_id in self.subscriptions:
            dest = self.subscriptions.pop(sub_id)
            logger.info(f"Unsubscribed: id={sub_id}, destination={dest}")

    async def _handle_send(self, headers: dict, body: str):
        dest = headers.get("destination", "")
        logger.info(f"Message sent to {dest}: {body}")

        # Echo / Simulate Cloud reaction: if balance sends login/status, reply back with a command on subscribed destination
        if dest.startswith("/app/"):
            # Typical Spring mapping: incoming /app/... might trigger notification on /topic/...
            reply_dest = dest.replace("/app/", "/topic/")
            await self.broadcast_message(reply_dest, json.dumps({
                "status": "ACK",
                "receivedDestination": dest,
                "receivedBody": body
            }))

    async def _handle_disconnect(self, headers: dict):
        receipt = headers.get("receipt")
        if receipt:
            await self.send_raw(f"RECEIPT\nreceipt-id:{receipt}\n\n\x00")
        self.connected = False
        logger.info("Client disconnected.")

    async def broadcast_message(self, destination: str, payload: str):
        for sub_id, dest in self.subscriptions.items():
            if dest == destination:
                msg = (
                    "MESSAGE\n"
                    f"destination:{destination}\n"
                    f"subscription:{sub_id}\n"
                    f"message-id:msg-{asyncio.get_event_loop().time()}\n"
                    "content-type:application/json\n"
                    f"content-length:{len(payload.encode('utf-8'))}\n"
                    "\n"
                    f"{payload}\x00"
                )
                logger.info(f"Dispatching MESSAGE to subscription {sub_id} ({destination}): {payload}")
                await self.send_raw(msg)


# Simple WebSocket server using either `websockets` or custom raw TCP
async def run_websocket_server(host="0.0.0.0", port=8080):
    try:
        import websockets
    except ImportError:
        logger.error("Please install 'websockets': pip install websockets")
        logger.info("Starting fallback raw TCP STOMP server on port %d...", port)
        await run_raw_tcp_server(host, port)
        return

    logger.info(f"Starting STOMP over WebSocket server on ws://{host}:{port}/stomp/websocket")

    async def ws_handler(websocket):
        logger.info(f"New client connected from {websocket.remote_address}")

        async def send_raw(data: str):
            await websocket.send(data)

        handler = StompProtocolHandler(send_raw)

        # Periodic background task sending simulated commands to the balance (e.g. tare / new job)
        async def periodic_cloud_commands():
            cmd_count = 0
            while True:
                await asyncio.sleep(5)
                if handler.connected and "/topic/commands" in handler.subscriptions.values():
                    cmd_count += 1
                    await handler.broadcast_message(
                        "/topic/commands",
                        json.dumps({"commandId": cmd_count, "action": "TARE", "source": "Cloud"})
                    )

        task = asyncio.create_task(periodic_cloud_commands())
        try:
            async for message in websocket:
                if isinstance(message, bytes):
                    message = message.decode("utf-8", errors="replace")
                await handler.handle_raw_data(message)
        except Exception as e:
            logger.info(f"Connection ended: {e}")
        finally:
            task.cancel()

    async with websockets.serve(ws_handler, host, port):
        await asyncio.Future()  # run forever


async def run_raw_tcp_server(host="0.0.0.0", port=8080):
    logger.info(f"Starting raw TCP STOMP server on {host}:{port}")

    async def client_connected(reader, writer):
        addr = writer.get_extra_info("peername")
        logger.info(f"New TCP connection from {addr}")

        async def send_raw(data: str):
            writer.write(data.encode("utf-8"))
            await writer.drain()

        handler = StompProtocolHandler(send_raw)
        try:
            while True:
                data = await reader.read(4096)
                if not data:
                    break
                await handler.handle_raw_data(data.decode("utf-8", errors="replace"))
        except Exception as e:
            logger.info(f"TCP connection ended: {e}")
        finally:
            writer.close()
            await writer.wait_closed()

    server = await asyncio.start_server(client_connected, host, port)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    port = 8080
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    try:
        asyncio.run(run_websocket_server(port=port))
    except KeyboardInterrupt:
        logger.info("Server stopped.")
