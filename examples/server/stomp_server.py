#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 freedy79
"""
STOMP Ping-Pong Game Test Server over WebSocket.
Completely device-independent simulation.

Rules:
1. Server serves a PING towards a random direction: LEFT, MIDDLE, or RIGHT.
2. Client responds to /app/pong with its guessed direction (e.g. LEFT, MIDDLE, RIGHT)
   or "MISS".
3. Server evaluates:
   - If directions match -> HIT! Point for Client.
   - If directions mismatch or client missed -> Point for Server.
4. Server announces score & serves next ball.
"""

import asyncio
import json
import logging
import random
import sys
from typing import Dict

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("ping_pong_server")

DIRECTIONS = ["LEFT", "MIDDLE", "RIGHT"]
DIRECTION_INDEX = {"LEFT": 0, "MIDDLE": 1, "RIGHT": 2}


def reach_probability(distance: int) -> float:
    """Chance to reach the ball depending on how far the player has to move."""
    return {0: 0.90, 1: 0.60}.get(distance, 0.30)


class PingPongStompHandler:
    def __init__(self, send_raw_fn):
        self.send_raw = send_raw_fn
        self.subscriptions: Dict[str, str] = {}
        self.connected = False
        self.client_score = 0
        self.server_score = 0
        self.current_direction = None
        self.last_server_direction = None
        self.server_position = "MIDDLE"
        self.rally_hits = 0
        self.round_number = 0
        self.game_task = None

    async def handle_raw_data(self, data: str):
        frames = data.split("\x00")
        for raw_frame in frames:
            frame_trimmed = raw_frame.strip()
            if not frame_trimmed:
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
                body = "\n".join(lines[idx + 1 :])
                break
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip()] = v.strip()
            idx += 1

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

    async def _handle_connect(self, headers: dict):
        self.connected = True
        logger.info("🏓 Player connected! Ready for Ping-Pong match.")
        response = (
            "CONNECTED\n"
            "version:1.2\n"
            "session:ping-pong-game-001\n"
            "server:Stomp-Ping-Pong-Host/1.0\n"
            "heart-beat:10000,10000\n"
            "\n"
            "\x00"
        )
        await self.send_raw(response)

    async def _handle_subscribe(self, headers: dict):
        sub_id = headers.get("id", "0")
        dest = headers.get("destination", "")
        self.subscriptions[sub_id] = dest
        logger.info(f"Subscribed: id={sub_id}, destination={dest}")

        if dest == "/topic/game" and not self.game_task:
            self.game_task = asyncio.create_task(self._start_game())

    async def _handle_unsubscribe(self, headers: dict):
        sub_id = headers.get("id", "")
        self.subscriptions.pop(sub_id, None)

    async def _handle_send(self, headers: dict, body: str):
        dest = headers.get("destination", "")
        if dest == "/app/rematch":
            try:
                data = json.loads(body)
                rematch = data.get("rematch", True)
            except Exception:
                rematch = True

            if rematch:
                logger.info("🔁 Rematch accepted by Client! Starting new game...")
                self.client_score = 0
                self.server_score = 0
                self.round_number = 0
                self.last_server_direction = None
                self.server_position = "MIDDLE"
                self.rally_hits = 0
                if self.game_task:
                    self.game_task.cancel()
                self.game_task = asyncio.create_task(self._start_game())
            else:
                logger.info("👋 Client declined rematch. Good game!")
                await self.broadcast_message("/topic/game", json.dumps({
                    "type": "GAME_OVER",
                    "message": "Thanks for playing! Game closed."
                }))
            return

        if dest == "/app/pong":
            direction = ""
            status = "PONG"
            try:
                data = json.loads(body)
                direction = data.get("direction", "").upper()
                status = data.get("status", "PONG").upper()
            except Exception:
                direction = body.strip().upper()

            logger.info(f"🎾 Client returned: status={status}, target={direction} (ball was played to {self.current_direction})")

            if status == "PONG" and direction in DIRECTION_INDEX:
                # The server now has to reach the ball the client played.
                distance = abs(DIRECTION_INDEX[direction] - DIRECTION_INDEX[self.server_position])
                reached = random.random() < reach_probability(distance)
                from_position = self.server_position
                self.server_position = direction

                if reached:
                    self.rally_hits += 1
                    logger.info(f"🛡️ Server reached the ball ({from_position} -> {direction}) - rally continues "
                                f"({self.rally_hits} hits)")
                    asyncio.create_task(self._rally_return_delay(0.8))
                    return

                self.client_score += 1
                outcome = "CLIENT_POINT"
                detail = (f"Server could not reach {direction} coming from {from_position} "
                          f"after {self.rally_hits} rally hits (Client Point)!")
            else:
                self.server_score += 1
                outcome = "CLIENT_MISS"
                detail = (f"Client could not reach the ball played to {self.current_direction} "
                          f"after {self.rally_hits} rally hits (Server Point)!")

            logger.info(f"🏆 Round {self.round_number} -> {outcome} after {self.rally_hits} hits! "
                        f"Score: Client {self.client_score} : {self.server_score} Server")

            # Check if game has reached winning score (11 points)
            if self.client_score >= 11 or self.server_score >= 11:
                winner = "CLIENT" if self.client_score >= 11 else "SERVER"
                logger.info(f"🎉 MATCH WON BY {winner}! Final Score: Client {self.client_score} - {self.server_score} Server")

                await self.broadcast_message("/topic/game", json.dumps({
                    "type": "MATCH_FINISH",
                    "winner": winner,
                    "finalClientScore": self.client_score,
                    "finalServerScore": self.server_score,
                    "round": self.round_number,
                    "outcome": outcome,
                    "detail": detail,
                    "rallyHits": self.rally_hits,
                    "message": f"Match over! Winner: {winner}! Waiting for rematch decision..."
                }))
            else:
                # Broadcast regular score update
                await self.broadcast_message("/topic/game", json.dumps({
                    "type": "SCORE",
                    "round": self.round_number,
                    "outcome": outcome,
                    "detail": detail,
                    "rallyHits": self.rally_hits,
                    "clientScore": self.client_score,
                    "serverScore": self.server_score
                }))

                # Serve next ball after short timeout
                asyncio.create_task(self._next_serve_delay(1.5))

    async def _start_game(self):
        await asyncio.sleep(1.0)
        logger.info("🎮 Match is starting now!")
        await self.broadcast_message("/topic/game", json.dumps({
            "type": "GAME_START",
            "targetScore": 11,
            "message": "Welcome to STOMP Ping-Pong! First to 11 points wins! Starting in 2s..."
        }))
        await asyncio.sleep(2.0)
        await self._serve()

    async def _next_serve_delay(self, delay: float):
        await asyncio.sleep(delay)
        if self.connected:
            await self._serve()

    async def _rally_return_delay(self, delay: float):
        await asyncio.sleep(delay)
        if self.connected:
            await self._play_ball("RALLY")

    async def _serve(self):
        self.round_number += 1
        self.rally_hits = 0
        self.server_position = "MIDDLE"
        await self._play_ball("SERVE")

    async def _play_ball(self, shot: str):
        # Nobody may play the same direction twice in a row.
        possible = [d for d in DIRECTIONS if d != self.last_server_direction]
        self.current_direction = random.choice(possible)
        self.last_server_direction = self.current_direction

        label = "serves" if shot == "SERVE" else "returns"
        logger.info(f"⚡ [Round {self.round_number}] Server {label} PING towards -> {self.current_direction} "
                    f"(rally hits: {self.rally_hits})")

        await self.broadcast_message("/topic/game", json.dumps({
            "type": "PING",
            "shot": shot,
            "round": self.round_number,
            "rallyHits": self.rally_hits,
            "direction": self.current_direction,
            "message": f"Server {label} the ball towards {self.current_direction}!"
        }))

    async def _handle_disconnect(self, headers: dict):
        self.connected = False
        if self.game_task:
            self.game_task.cancel()
        logger.info(f"Match ended. Final Score: Client {self.client_score} - {self.server_score} Server")

    async def broadcast_message(self, destination: str, payload: str):
        if not self.connected:
            return
        for sub_id, dest in list(self.subscriptions.items()):
            if dest == destination:
                msg = (
                    "MESSAGE\n"
                    f"destination:{destination}\n"
                    f"subscription:{sub_id}\n"
                    f"message-id:game-{self.round_number}-{random.randint(100, 999)}\n"
                    "content-type:application/json\n"
                    f"content-length:{len(payload.encode('utf-8'))}\n"
                    "\n"
                    f"{payload}\x00"
                )
                try:
                    await self.send_raw(msg)
                except Exception as e:
                    logger.warning(f"Could not deliver message to {sub_id}: {e}")
                    self.connected = False


async def run_websocket_server(host="0.0.0.0", port=8080):
    try:
        import websockets
    except ImportError:
        logger.error("Please install 'websockets': pip install websockets")
        return

    logger.info(f"Starting STOMP Ping-Pong Game Server on ws://{host}:{port}/stomp/websocket")

    async def ws_handler(websocket):
        logger.info(f"Client player connected from {websocket.remote_address}")

        async def send_raw(data: str):
            await websocket.send(data)

        handler = PingPongStompHandler(send_raw)
        try:
            async for message in websocket:
                if isinstance(message, bytes):
                    message = message.decode("utf-8", errors="replace")
                await handler.handle_raw_data(message)
        except Exception as e:
            logger.info(f"Connection closed: {e}")
        finally:
            if handler.game_task:
                handler.game_task.cancel()

    async with websockets.serve(ws_handler, host, port):
        await asyncio.Future()


if __name__ == "__main__":
    port = 8080
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    try:
        asyncio.run(run_websocket_server(port=port))
    except KeyboardInterrupt:
        logger.info("Server stopped.")
