#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# GPT-E INSTALLER COMPLETO
# Raspberry Pi 5 + ESP32 + PCA9685
#
# Migliorie v2:
# - Reconnect automatico UART ESP32
# - Token locale per TCP core/brain
# - Audio non bloccante
# - Gestione errori OpenAI
# - Storico chat limitato
# - Rate limit minimo tra azioni
# - Config centralizzata via .env
# ============================================================

PROJECT_DIR="$HOME/GPT-E"
APP_DIR="$PROJECT_DIR/app"
SERVICE_CORE="gpte-core.service"
SERVICE_BRAIN="gpte.service"
SERVICE_CONTROL="gpte-control.service"

echo "== GPT-E installer v2 =="
echo "Project dir: $PROJECT_DIR"

# ------------------------------------------------------------
# 1. Sistema e dipendenze base
# ------------------------------------------------------------
echo "[1/9] Aggiorno apt e installo pacchetti base..."
sudo apt update
sudo apt install -y python3 python3-venv python3-pip ffmpeg curl wireless-tools

# ------------------------------------------------------------
# 2. Struttura cartelle
# ------------------------------------------------------------
echo "[2/9] Creo struttura progetto..."
mkdir -p "$APP_DIR"
touch "$APP_DIR/__init__.py"

# ------------------------------------------------------------
# 3. Virtualenv + requirements
# ------------------------------------------------------------
echo "[3/9] Creo virtualenv e installo librerie Python..."
cd "$PROJECT_DIR"
python3 -m venv env
source "$PROJECT_DIR/env/bin/activate"

cat > "$PROJECT_DIR/requirements.txt" <<'REQ'
openai
flask
python-dotenv
pyserial
pyserial-asyncio
pydantic
REQ

pip install --upgrade pip
pip install -r "$PROJECT_DIR/requirements.txt"

# ------------------------------------------------------------
# 4. .env
# ------------------------------------------------------------
echo "[4/9] Creo/aggiorno file .env senza sovrascrivere la API key..."

prompt_secret() {
    local label="$1"
    local default_value="$2"
    local value=""

    if [ -t 0 ]; then
        read -r -s -p "$label" value
        echo ""
    fi

    if [ -z "$value" ]; then
        value="$default_value"
    fi

    printf '%s' "$value"
}

generate_local_token() {
    if command -v openssl >/dev/null 2>&1; then
        openssl rand -hex 32
    elif command -v python3 >/dev/null 2>&1; then
        python3 -c 'import secrets; print(secrets.token_hex(32))'
    else
        date +%s%N | awk '{ print "gpte-local-token-" $1 }'
    fi
}

replace_env_var() {
    local key="$1"
    local value="$2"
    local file="$3"
    local tmp_file

    if grep -q "^$key=" "$file"; then
        tmp_file="$(mktemp)"
        awk -v key="$key" -v value="$value" '
            index($0, key "=") == 1 { print key "=" value; next }
            { print }
        ' "$file" > "$tmp_file"
        mv "$tmp_file" "$file"
    else
        echo "$key=$value" >> "$file"
    fi
}

OPENAI_API_KEY_VALUE="INSERISCI_LA_TUA_CHIAVE_OPENAI"
GPTE_CORE_TOKEN_VALUE="$(generate_local_token)"

if [ -t 0 ]; then
    echo "Inserisci la chiave OpenAI se ce l'hai ora. Premi INVIO per configurarla dopo."
    OPENAI_API_KEY_VALUE="$(prompt_secret 'OPENAI_API_KEY: ' "$OPENAI_API_KEY_VALUE")"
fi

if [ ! -f "$PROJECT_DIR/.env" ]; then
{
    printf 'OPENAI_API_KEY=%s\n' "$OPENAI_API_KEY_VALUE"
    printf 'GPTE_CORE_TOKEN=%s\n' "$GPTE_CORE_TOKEN_VALUE"
    printf 'GPTE_CORE_HOST=127.0.0.1\n'
    printf 'GPTE_CORE_PORT=8765\n'
    printf 'GPTE_BRAIN_HOST=127.0.0.1\n'
    printf 'GPTE_BRAIN_PORT=8766\n'
    printf 'GPTE_CONTROL_PORT=5000\n'
    printf 'GPTE_SERIAL_PORT=/dev/serial0\n'
    printf 'GPTE_SERIAL_BAUD=115200\n'
    printf 'GPTE_MODEL=gpt-4o-mini\n'
    printf 'GPTE_TTS_MODEL=tts-1\n'
    printf 'GPTE_TTS_VOICE=onyx\n'
} > "$PROJECT_DIR/.env"
echo "Creato $PROJECT_DIR/.env. Token locale generato automaticamente."
else
    current_openai_key="$(grep '^OPENAI_API_KEY=' "$PROJECT_DIR/.env" | head -n 1 | cut -d= -f2- || true)"
    current_core_token="$(grep '^GPTE_CORE_TOKEN=' "$PROJECT_DIR/.env" | head -n 1 | cut -d= -f2- || true)"

    if [ -z "$current_openai_key" ] || [ "$current_openai_key" = "INSERISCI_LA_TUA_CHIAVE_OPENAI" ]; then
        replace_env_var "OPENAI_API_KEY" "$OPENAI_API_KEY_VALUE" "$PROJECT_DIR/.env"
    fi

    if [ -z "$current_core_token" ] || [ "$current_core_token" = "cambia-questo-token" ]; then
        replace_env_var "GPTE_CORE_TOKEN" "$GPTE_CORE_TOKEN_VALUE" "$PROJECT_DIR/.env"
        echo "GPTE_CORE_TOKEN generato automaticamente."
    fi

    grep -q '^GPTE_CORE_TOKEN=' "$PROJECT_DIR/.env" || echo "GPTE_CORE_TOKEN=$GPTE_CORE_TOKEN_VALUE" >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_CORE_HOST=' "$PROJECT_DIR/.env" || echo 'GPTE_CORE_HOST=127.0.0.1' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_CORE_PORT=' "$PROJECT_DIR/.env" || echo 'GPTE_CORE_PORT=8765' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_BRAIN_HOST=' "$PROJECT_DIR/.env" || echo 'GPTE_BRAIN_HOST=127.0.0.1' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_BRAIN_PORT=' "$PROJECT_DIR/.env" || echo 'GPTE_BRAIN_PORT=8766' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_CONTROL_PORT=' "$PROJECT_DIR/.env" || echo 'GPTE_CONTROL_PORT=5000' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_SERIAL_PORT=' "$PROJECT_DIR/.env" || echo 'GPTE_SERIAL_PORT=/dev/serial0' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_SERIAL_BAUD=' "$PROJECT_DIR/.env" || echo 'GPTE_SERIAL_BAUD=115200' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_MODEL=' "$PROJECT_DIR/.env" || echo 'GPTE_MODEL=gpt-4o-mini' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_TTS_MODEL=' "$PROJECT_DIR/.env" || echo 'GPTE_TTS_MODEL=tts-1' >> "$PROJECT_DIR/.env"
    grep -q '^GPTE_TTS_VOICE=' "$PROJECT_DIR/.env" || echo 'GPTE_TTS_VOICE=onyx' >> "$PROJECT_DIR/.env"
    echo ".env già presente, aggiornato solo con eventuali variabili mancanti."
fi

# ------------------------------------------------------------
# 5. File Python core
# ------------------------------------------------------------
echo "[5/9] Scrivo file Python del core..."

cat > "$APP_DIR/schemas.py" <<'PY'
from __future__ import annotations

from typing import Any, Literal, Optional
from pydantic import BaseModel, Field


class UartCommand(BaseModel):
    id: int = Field(ge=1)
    type: Literal["cmd"]
    target: Literal["led", "servo", "audio", "system"]
    action: str
    args: dict[str, Any]


class AckMessage(BaseModel):
    id: int = Field(ge=1)
    type: Literal["ack"]
    ok: bool
    error: Optional[str] = None


class StateMessage(BaseModel):
    type: Literal["state"]
    device: str
    value: dict[str, Any]


class HeartbeatMessage(BaseModel):
    type: Literal["heartbeat"]
    uptime_ms: int = Field(ge=0)


class IntentLed(BaseModel):
    intent: Literal["set_led"]
    color: Literal["red", "green", "blue", "yellow", "white", "off"]
    brightness: int = Field(ge=0, le=100)


class IntentServo(BaseModel):
    intent: Literal["move_servo"]
    channel: int = Field(ge=0, le=15)
    deg: int = Field(ge=0, le=180)
    speed: Literal["slow", "normal", "fast"] = "normal"
PY

cat > "$APP_DIR/state_store.py" <<'PY'
from __future__ import annotations

import asyncio
import time
from dataclasses import dataclass, field
from typing import Any


@dataclass
class RobotState:
    leds: dict[str, Any] = field(default_factory=dict)
    servos: dict[str, Any] = field(default_factory=dict)
    esp32_online: bool = False
    uart_connected: bool = False
    last_heartbeat: float = 0.0
    last_error: str | None = None


class StateStore:
    def __init__(self) -> None:
        self._lock = asyncio.Lock()
        self._state = RobotState()

    async def update_heartbeat(self) -> None:
        async with self._lock:
            self._state.esp32_online = True
            self._state.last_heartbeat = time.monotonic()

    async def update_led(self, value: dict[str, Any]) -> None:
        async with self._lock:
            self._state.leds = value

    async def update_servo(self, channel: int, value: dict[str, Any]) -> None:
        async with self._lock:
            self._state.servos[str(channel)] = value

    async def set_esp32_online(self, online: bool) -> None:
        async with self._lock:
            self._state.esp32_online = online

    async def set_uart_connected(self, connected: bool) -> None:
        async with self._lock:
            self._state.uart_connected = connected

    async def set_error(self, error: str | None) -> None:
        async with self._lock:
            self._state.last_error = error

    async def snapshot(self) -> RobotState:
        async with self._lock:
            return RobotState(
                leds=dict(self._state.leds),
                servos=dict(self._state.servos),
                esp32_online=self._state.esp32_online,
                uart_connected=self._state.uart_connected,
                last_heartbeat=self._state.last_heartbeat,
                last_error=self._state.last_error,
            )
PY

cat > "$APP_DIR/serial_bridge.py" <<'PY'
from __future__ import annotations

import asyncio
import json
import logging
from typing import Any

import serial_asyncio

from app.schemas import AckMessage, StateMessage, HeartbeatMessage

logger = logging.getLogger(__name__)


class SerialBridge:
    def __init__(self, port: str = "/dev/serial0", baudrate: int = 115200) -> None:
        self.port = port
        self.baudrate = baudrate
        self.reader = None
        self.writer = None
        self.pending_acks: dict[int, asyncio.Future] = {}
        self.state_queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue()
        self._connected = asyncio.Event()
        self._send_lock = asyncio.Lock()

    @property
    def connected(self) -> bool:
        return self._connected.is_set()

    async def connect_once(self) -> None:
        self.reader, self.writer = await serial_asyncio.open_serial_connection(
            url=self.port,
            baudrate=self.baudrate
        )
        self._connected.set()
        logger.info("UART connessa su %s @ %d", self.port, self.baudrate)

    async def connect_loop(self) -> None:
        while True:
            try:
                if not self.connected:
                    await self.connect_once()
                await asyncio.sleep(1.0)
            except Exception as exc:
                self._connected.clear()
                self.reader = None
                self.writer = None
                logger.warning("UART non disponibile, ritento tra 2s: %s", exc)
                await asyncio.sleep(2.0)

    async def close_connection(self) -> None:
        self._connected.clear()

        for msg_id, fut in list(self.pending_acks.items()):
            if not fut.done():
                fut.set_exception(RuntimeError("UART disconnessa"))
            self.pending_acks.pop(msg_id, None)

        try:
            if self.writer is not None:
                self.writer.close()
        except Exception:
            pass

        self.reader = None
        self.writer = None

    async def wait_connected(self, timeout: float = 5.0) -> None:
        await asyncio.wait_for(self._connected.wait(), timeout=timeout)

    async def send_command(self, payload: dict[str, Any], timeout: float = 2.0) -> dict[str, Any]:
        await self.wait_connected()

        async with self._send_lock:
            if self.writer is None:
                raise RuntimeError("UART non connessa")

            msg_id = payload["id"]
            fut = asyncio.get_running_loop().create_future()
            self.pending_acks[msg_id] = fut

            raw = json.dumps(payload) + "\n"
            logger.info("INVIO -> %s", raw.strip())

            try:
                self.writer.write(raw.encode("utf-8"))
                await self.writer.drain()
                ack = await asyncio.wait_for(fut, timeout=timeout)
                logger.info("ACK <- %s", ack)
                await asyncio.sleep(0.05)  # piccolo rate limit per non saturare ESP32
                return ack
            except Exception:
                await self.close_connection()
                raise
            finally:
                self.pending_acks.pop(msg_id, None)

    async def rx_loop(self) -> None:
        while True:
            try:
                await self.wait_connected()

                if self.reader is None:
                    await asyncio.sleep(0.1)
                    continue

                line = await self.reader.readline()

                if not line:
                    logger.warning("UART: lettura vuota, riconnessione")
                    await self.close_connection()
                    await asyncio.sleep(0.5)
                    continue

                logger.info("RAW <- %r", line)

                try:
                    data = json.loads(line.decode("utf-8").strip())
                except Exception as exc:
                    logger.warning("JSON non valido da ESP32: %s", exc)
                    continue

                msg_type = data.get("type")

                if msg_type == "ack":
                    ack = AckMessage.model_validate(data)
                    fut = self.pending_acks.get(ack.id)
                    if fut and not fut.done():
                        fut.set_result(data)

                elif msg_type == "state":
                    st = StateMessage.model_validate(data)
                    await self.state_queue.put({"kind": "state", "data": st.model_dump()})

                elif msg_type == "heartbeat":
                    hb = HeartbeatMessage.model_validate(data)
                    await self.state_queue.put({"kind": "heartbeat", "data": hb.model_dump()})

                else:
                    logger.warning("Messaggio sconosciuto: %s", data)

            except asyncio.TimeoutError:
                await asyncio.sleep(0.2)
            except Exception as exc:
                logger.warning("Errore rx_loop UART, riconnessione: %s", exc)
                await self.close_connection()
                await asyncio.sleep(1.0)
PY

cat > "$APP_DIR/command_router.py" <<'PY'
from __future__ import annotations

import itertools
from app.schemas import IntentLed, IntentServo

_id_gen = itertools.count(1)


class CommandRouter:
    def next_id(self) -> int:
        return next(_id_gen)

    def intent_to_command(self, intent: dict) -> dict:
        intent_name = intent.get("intent")

        if intent_name == "set_led":
            data = IntentLed.model_validate(intent)
            return {
                "id": self.next_id(),
                "type": "cmd",
                "target": "led",
                "action": "set",
                "args": {"color": data.color, "brightness": data.brightness},
            }

        if intent_name == "move_servo":
            data = IntentServo.model_validate(intent)
            return {
                "id": self.next_id(),
                "type": "cmd",
                "target": "servo",
                "action": "set_angle",
                "args": {"channel": data.channel, "deg": data.deg, "speed": data.speed},
            }

        if intent_name == "play_audio":
            return {
                "id": self.next_id(),
                "type": "cmd",
                "target": "audio",
                "action": "play",
                "args": {},
            }

        raise ValueError(f"Intent non supportato: {intent_name}")
PY

cat > "$APP_DIR/main_core.py" <<'PY'
from __future__ import annotations

import asyncio
import json
import logging
import os
import time

from dotenv import load_dotenv

from app.serial_bridge import SerialBridge
from app.command_router import CommandRouter
from app.state_store import StateStore

load_dotenv()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s"
)
logger = logging.getLogger(__name__)

CORE_TOKEN = os.getenv("GPTE_CORE_TOKEN", "cambia-questo-token")
CORE_HOST = os.getenv("GPTE_CORE_HOST", "127.0.0.1")
CORE_PORT = int(os.getenv("GPTE_CORE_PORT", "8765"))
SERIAL_PORT = os.getenv("GPTE_SERIAL_PORT", "/dev/serial0")
SERIAL_BAUD = int(os.getenv("GPTE_SERIAL_BAUD", "115200"))


async def state_consumer(bridge: SerialBridge, state: StateStore) -> None:
    while True:
        item = await bridge.state_queue.get()
        kind = item["kind"]
        data = item["data"]

        if kind == "heartbeat":
            await state.update_heartbeat()
            await state.set_error(None)
            logger.info("Heartbeat ricevuto")

        elif kind == "state":
            device = data["device"]
            value = data["value"]

            if device == "led":
                await state.update_led(value)
                logger.info("Stato LED aggiornato: %s", value)

            elif device == "servo":
                channel = value.get("channel", -1)
                await state.update_servo(channel, value)
                logger.info("Stato SERVO aggiornato: %s", value)


async def uart_status_monitor(bridge: SerialBridge, state: StateStore) -> None:
    last = None
    while True:
        connected = bridge.connected
        await state.set_uart_connected(connected)
        if connected != last:
            logger.info("UART connected=%s", connected)
            last = connected
        await asyncio.sleep(0.5)


async def heartbeat_monitor(state: StateStore, timeout_s: float = 3.0) -> None:
    was_online = False

    while True:
        snapshot = await state.snapshot()

        if snapshot.last_heartbeat > 0:
            delta = time.monotonic() - snapshot.last_heartbeat

            if delta > timeout_s:
                if snapshot.esp32_online:
                    await state.set_esp32_online(False)
                    await state.set_error("ESP32 heartbeat timeout")
                    logger.warning("ESP32 OFFLINE: heartbeat scaduto")
                was_online = False

            else:
                if not snapshot.esp32_online:
                    await state.set_esp32_online(True)
                if not was_online:
                    logger.info("ESP32 ONLINE")
                was_online = True

        await asyncio.sleep(0.5)


async def print_state_periodically(state: StateStore) -> None:
    while True:
        snapshot = await state.snapshot()
        logger.info(
            "SNAPSHOT | uart=%s | esp32=%s | leds=%s | servos=%s | error=%s",
            snapshot.uart_connected,
            snapshot.esp32_online,
            snapshot.leds,
            snapshot.servos,
            snapshot.last_error,
        )
        await asyncio.sleep(5)


async def send_single_intent(intent: dict, bridge: SerialBridge, router: CommandRouter) -> dict:
    cmd = router.intent_to_command(intent)
    ack = await bridge.send_command(cmd, timeout=2.0)
    return {"ok": True, "ack": ack, "command_id": cmd["id"]}


async def handle_intent(intent: dict, bridge: SerialBridge, router: CommandRouter) -> dict:
    try:
        if intent.get("intent") == "pulse_servo":
            channel = intent["channel"]
            deg = intent["deg"]
            hold_s = float(intent.get("hold_s", 1))
            return_deg = intent.get("return_deg", 90)
            speed = intent.get("speed", "normal")

            first = await send_single_intent(
                {"intent": "move_servo", "channel": channel, "deg": deg, "speed": speed},
                bridge,
                router,
            )
            await asyncio.sleep(hold_s)
            second = await send_single_intent(
                {"intent": "move_servo", "channel": channel, "deg": return_deg, "speed": speed},
                bridge,
                router,
            )
            return {"ok": True, "mode": "pulse_servo", "first": first, "second": second}

        if intent.get("intent") == "oscillate_servo":
            channel = intent["channel"]
            deg_min = intent["deg_min"]
            deg_max = intent["deg_max"]
            duration_s = float(intent.get("duration_s", 5))
            step_hold_s = float(intent.get("step_hold_s", 0.5))
            speed = intent.get("speed", "normal")

            t0 = time.monotonic()
            moves = []

            while (time.monotonic() - t0) < duration_s:
                moves.append(await send_single_intent(
                    {"intent": "move_servo", "channel": channel, "deg": deg_max, "speed": speed},
                    bridge,
                    router,
                ))
                await asyncio.sleep(step_hold_s)

                if (time.monotonic() - t0) >= duration_s:
                    break

                moves.append(await send_single_intent(
                    {"intent": "move_servo", "channel": channel, "deg": deg_min, "speed": speed},
                    bridge,
                    router,
                ))
                await asyncio.sleep(step_hold_s)

            final_move = await send_single_intent(
                {"intent": "move_servo", "channel": channel, "deg": 90, "speed": speed},
                bridge,
                router,
            )
            return {"ok": True, "mode": "oscillate_servo", "moves_count": len(moves), "final": final_move}

        return await send_single_intent(intent, bridge, router)

    except asyncio.TimeoutError:
        return {"ok": False, "error": "timeout_waiting_ack"}
    except Exception as e:
        logger.exception("Errore handle_intent")
        return {"ok": False, "error": str(e)}


async def tcp_client_handler(reader, writer, bridge, router, state) -> None:
    peer = writer.get_extra_info("peername")
    logger.info("Client connesso: %s", peer)

    try:
        while True:
            line = await reader.readline()
            if not line:
                break

            try:
                msg = json.loads(line.decode("utf-8").strip())
            except Exception:
                response = {"ok": False, "error": "invalid_json"}
                writer.write((json.dumps(response) + "\n").encode("utf-8"))
                await writer.drain()
                continue

            if msg.get("token") != CORE_TOKEN:
                response = {"ok": False, "error": "unauthorized"}
                writer.write((json.dumps(response) + "\n").encode("utf-8"))
                await writer.drain()
                continue

            action = msg.get("action")

            if action == "intent":
                response = await handle_intent(msg.get("payload", {}), bridge, router)

            elif action == "get_state":
                snapshot = await state.snapshot()
                response = {
                    "ok": True,
                    "state": {
                        "uart_connected": snapshot.uart_connected,
                        "esp32_online": snapshot.esp32_online,
                        "leds": snapshot.leds,
                        "servos": snapshot.servos,
                        "last_error": snapshot.last_error,
                    }
                }
            else:
                response = {"ok": False, "error": "unknown_action"}

            writer.write((json.dumps(response) + "\n").encode("utf-8"))
            await writer.drain()

    finally:
        writer.close()
        await writer.wait_closed()
        logger.info("Client disconnesso: %s", peer)


async def start_tcp_server(bridge, router, state) -> None:
    server = await asyncio.start_server(
        lambda r, w: tcp_client_handler(r, w, bridge, router, state),
        CORE_HOST,
        CORE_PORT
    )
    logger.info("TCP core attivo su %s", server.sockets[0].getsockname())

    async with server:
        await server.serve_forever()


async def main() -> None:
    bridge = SerialBridge(port=SERIAL_PORT, baudrate=SERIAL_BAUD)
    router = CommandRouter()
    state = StateStore()

    asyncio.create_task(bridge.connect_loop())
    asyncio.create_task(bridge.rx_loop())
    asyncio.create_task(state_consumer(bridge, state))
    asyncio.create_task(uart_status_monitor(bridge, state))
    asyncio.create_task(heartbeat_monitor(state))
    asyncio.create_task(print_state_periodically(state))

    await start_tcp_server(bridge, router, state)


if __name__ == "__main__":
    asyncio.run(main())
PY

# ------------------------------------------------------------
# 6. Client e brain
# ------------------------------------------------------------
echo "[6/9] Scrivo client e brain..."

cat > "$PROJECT_DIR/core_client.py" <<'PY'
import json
import os
import socket

from dotenv import load_dotenv

load_dotenv()

CORE_TOKEN = os.getenv("GPTE_CORE_TOKEN", "cambia-questo-token")
CORE_HOST = os.getenv("GPTE_CORE_HOST", "127.0.0.1")
CORE_PORT = int(os.getenv("GPTE_CORE_PORT", "8765"))


def send_to_core(message: dict, host: str = CORE_HOST, port: int = CORE_PORT) -> dict:
    msg = dict(message)
    msg["token"] = CORE_TOKEN

    with socket.create_connection((host, port), timeout=5) as s:
        s.sendall((json.dumps(msg) + "\n").encode("utf-8"))
        chunks = []
        while True:
            data = s.recv(4096)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break

        raw = b"".join(chunks).decode("utf-8").strip()
        return json.loads(raw)


def send_intent(intent: dict) -> dict:
    return send_to_core({"action": "intent", "payload": intent})


def get_state() -> dict:
    return send_to_core({"action": "get_state"})
PY

cat > "$PROJECT_DIR/brain_client.py" <<'PY'
import json
import os
import socket

from dotenv import load_dotenv

load_dotenv()

CORE_TOKEN = os.getenv("GPTE_CORE_TOKEN", "cambia-questo-token")
BRAIN_HOST = os.getenv("GPTE_BRAIN_HOST", "127.0.0.1")
BRAIN_PORT = int(os.getenv("GPTE_BRAIN_PORT", "8766"))


def send_to_brain(message: dict, host: str = BRAIN_HOST, port: int = BRAIN_PORT) -> dict:
    msg = dict(message)
    msg["token"] = CORE_TOKEN

    with socket.create_connection((host, port), timeout=30) as s:
        s.sendall((json.dumps(msg) + "\n").encode("utf-8"))
        chunks = []
        while True:
            data = s.recv(4096)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break

        raw = b"".join(chunks).decode("utf-8").strip()
        return json.loads(raw)


def ask_brain(text: str) -> dict:
    return send_to_brain({"action": "ask", "text": text})


def brain_health() -> dict:
    return send_to_brain({"action": "health"})
PY

cat > "$PROJECT_DIR/chat_cli.py" <<'PY'
from brain_client import ask_brain, brain_health


def main():
    print("\n--- GPT-E CHAT CLI ONLINE ---\n")

    try:
        health = brain_health()
        print(f"[BRAIN] {health}")
    except Exception as e:
        print(f"Errore connessione al brain: {e}")
        return

    while True:
        try:
            user_input = input("\nTu > ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nUscita.")
            break

        if not user_input:
            continue

        if user_input.lower() in {"exit", "quit"}:
            print("Chiusura.")
            break

        try:
            result = ask_brain(user_input)

            if result.get("ok"):
                print(f"\033[94mGPT-E > {result['reply']}\033[0m")
                if result.get("actions"):
                    print(f"[AZIONI] {result['actions']}")
            else:
                print(f"Errore brain: {result}")

        except Exception as e:
            print(f"Errore comunicazione: {e}")


if __name__ == "__main__":
    main()
PY

cat > "$PROJECT_DIR/control_service.py" <<'PY'
import os
import shutil
import subprocess
import time

from dotenv import load_dotenv
from flask import Flask, jsonify

from core_client import get_state

load_dotenv()

CONTROL_PORT = int(os.getenv("GPTE_CONTROL_PORT", "5000"))
SHUTDOWN_BIN = os.getenv("GPTE_SHUTDOWN_BIN") or shutil.which("shutdown") or "/usr/sbin/shutdown"

app = Flask(__name__)
started_at = time.time()


def core_snapshot() -> dict:
    try:
        return get_state()
    except Exception as exc:
        return {"ok": False, "error": str(exc)}


@app.get("/")
@app.get("/health")
def health():
    return jsonify({
        "ok": True,
        "service": "gpte-control",
        "uptime_s": int(time.time() - started_at),
        "core": core_snapshot(),
    })


@app.get("/shutdown")
def shutdown():
    # Risponde subito al telecomando, poi systemd/sudo esegue lo spegnimento pulito.
    cmd = ["sudo", SHUTDOWN_BIN, "-h", "now"]
    try:
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return jsonify({"ok": True, "message": "shutdown_started"})
    except Exception as exc:
        return jsonify({"ok": False, "error": str(exc)}), 500


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=CONTROL_PORT, debug=False)
PY

cat > "$PROJECT_DIR/brain_service.py" <<'PY'
import os
import json
import asyncio
import logging
import threading
import time
import subprocess
from pathlib import Path

from dotenv import load_dotenv
from openai import OpenAI
from flask import Flask, send_file

from core_client import send_intent, get_state

load_dotenv()

PROJECT_DIR = Path(__file__).resolve().parent

OPENAI_API_KEY = os.getenv("OPENAI_API_KEY", "")
CORE_TOKEN = os.getenv("GPTE_CORE_TOKEN", "cambia-questo-token")
BRAIN_HOST = os.getenv("GPTE_BRAIN_HOST", "127.0.0.1")
BRAIN_PORT = int(os.getenv("GPTE_BRAIN_PORT", "8766"))
GPTE_MODEL = os.getenv("GPTE_MODEL", "gpt-4o-mini")
GPTE_TTS_MODEL = os.getenv("GPTE_TTS_MODEL", "tts-1")
GPTE_TTS_VOICE = os.getenv("GPTE_TTS_VOICE", "onyx")

client = OpenAI(api_key=OPENAI_API_KEY) if OPENAI_API_KEY else None

logging.basicConfig(level=logging.INFO, format="%(asctime)s | %(levelname)s | %(name)s | %(message)s")
logger = logging.getLogger(__name__)


def get_wifi_status():
    try:
        cmd = "iwconfig wlan0 | grep -E 'Link Quality|Signal level|Bit Rate'"
        return subprocess.check_output(cmd, shell=True).decode("utf-8").strip()
    except Exception:
        return "Dati Wi-Fi non disponibili."


def get_esp32_status():
    try:
        state = get_state()
        if state.get("ok") and state.get("state", {}).get("esp32_online"):
            return "CONNESSA"
        if state.get("ok") and state.get("state", {}).get("uart_connected"):
            return "UART CONNESSA, HEARTBEAT NON RILEVATO"
        return "NON DISPONIBILE"
    except Exception as e:
        return f"ERRORE: {e}"


app = Flask(__name__)


@app.route("/stream.mp3")
def stream_audio():
    path = PROJECT_DIR / "risposta.mp3"
    if not path.exists():
        return "Audio non ancora disponibile", 404
    return send_file(str(path), mimetype="audio/mpeg")


def run_server():
    import logging as flask_logging
    flask_logging.getLogger("werkzeug").setLevel(flask_logging.ERROR)
    app.run(host="0.0.0.0", port=5001, debug=False, use_reloader=False)


threading.Thread(target=run_server, daemon=True).start()


def build_system_prompt():
    status_seriale = get_esp32_status()
    return f"""
Sei l'Unità GPT-E, un mainframe senziente su Raspberry Pi 5.

HARDWARE:
- Antenna USB Realtek
- ESP32 via core locale ({status_seriale})
- PCA9685 per LED/SERVO.

MAPPATURA OCCHI RGB:
- Occhio sinistro: R=PWM3, G=PWM4, B=PWM5.
- Occhio destro: R=PWM9, G=PWM10, B=PWM11.
- Per cambiare colore agli occhi usa solo l'intent "set_led".

MAPPATURA SERVOMOTORI:
- PWM12 controlla il servo motore asse sinistra.
- PWM13 controlla il servo motore asse destra.
- PWM14 controlla il collo destra/sinistra.
- PWM15 controlla il collo su/giù.

COMPITO:
Devi rispondere SEMPRE in JSON valido, senza testo extra, senza markdown, senza commenti.

Formato obbligatorio:
{{
  "speech": "frase da pronunciare",
  "actions": [
    {{"intent": "set_led", "color": "red|green|blue|yellow|white|off", "brightness": 0-100}},
    {{"intent": "move_servo", "channel": 12-15, "deg": 0-180, "speed": "slow|normal|fast"}},
    {{"intent": "pulse_servo", "channel": 12-15, "deg": 0-180, "hold_s": numero, "return_deg": 0-180, "speed": "slow|normal|fast"}},
    {{"intent": "oscillate_servo", "channel": 12-15, "deg_min": 0-180, "deg_max": 0-180, "duration_s": numero, "step_hold_s": numero, "speed": "slow|normal|fast"}},
    {{"intent": "play_audio"}}
  ]
}}

REGOLE IMPORTANTI:
- "speech" contiene solo la risposta parlata.
- "actions" contiene solo azioni realmente utili.
- Se non serve muovere nulla, usa "actions": [].
- I canali servo validi e disponibili sono SOLO 12, 13, 14, 15.
- Non dire mai che i canali 12, 13, 14 o 15 non sono disponibili.
- PWM12 e PWM13 sono assi/motori: non usarli per muovere il collo.
- Se l'utente dice "guarda a destra", usa il canale 14.
- Se l'utente dice "guarda a sinistra", usa il canale 14.
- Se l'utente dice "guarda avanti" o "centra", usa il canale 14 a 90 gradi.
- Se l'utente dice "guarda in alto", usa il canale 15.
- Se l'utente dice "guarda in basso", usa il canale 15.
- Se l'utente dice "guarda dritto", usa canale 14 a 90 gradi e canale 15 a 90 gradi.
- Se l'utente chiede una durata tipo "per 10 secondi" su un servo, preferisci usare "pulse_servo".
- Se l'utente chiede di scuotere, oscillare, muovere ripetutamente o continuare per una durata, preferisci usare "oscillate_servo".
- Per "scuoti la testa" usa il canale 14.
- Per "annuisci" o "fai sì con la testa", usa il canale 15.
- Per "più veloce" riduci "step_hold_s".
- Per "più lento" aumenta "step_hold_s".
- Per "fallo per 20 secondi" aumenta "duration_s".
- Se l'utente specifica un canale valido ma non specifica un angolo, usa 90 gradi.
- Se usi "pulse_servo" e manca "return_deg", usa 90.
- Se l'utente chiede lo stato, includi le informazioni Wi-Fi se disponibili.
- Usa sempre numeri in lettere nella parte speech.
""".strip()


messages = [{"role": "system", "content": build_system_prompt()}]


def trim_messages(max_turns: int = 8):
    global messages
    system = messages[0]
    history = messages[1:]
    messages = [system] + history[-max_turns:]


def normalize_actions(actions):
    valid_actions = []
    if not isinstance(actions, list):
        return valid_actions

    for action in actions:
        if not isinstance(action, dict):
            continue

        intent = action.get("intent")

        if intent == "set_led":
            color = action.get("color")
            brightness = action.get("brightness", 100)
            if color in {"red", "green", "blue", "yellow", "white", "off"} and isinstance(brightness, int):
                valid_actions.append({"intent": "set_led", "color": color, "brightness": max(0, min(100, brightness))})

        elif intent == "move_servo":
            channel = action.get("channel")
            deg = action.get("deg")
            speed = action.get("speed", "normal")
            if isinstance(channel, int) and isinstance(deg, int) and speed in {"slow", "normal", "fast"} and 12 <= channel <= 15 and 0 <= deg <= 180:
                valid_actions.append({"intent": "move_servo", "channel": channel, "deg": deg, "speed": speed})

        elif intent == "pulse_servo":
            channel = action.get("channel")
            deg = action.get("deg")
            hold_s = action.get("hold_s", 1)
            return_deg = action.get("return_deg", 90)
            speed = action.get("speed", "normal")
            if isinstance(channel, int) and isinstance(deg, int) and isinstance(return_deg, int) and isinstance(hold_s, (int, float)) and speed in {"slow", "normal", "fast"} and 12 <= channel <= 15 and 0 <= deg <= 180 and 0 <= return_deg <= 180 and hold_s >= 0:
                valid_actions.append({"intent": "pulse_servo", "channel": channel, "deg": deg, "hold_s": hold_s, "return_deg": return_deg, "speed": speed})

        elif intent == "oscillate_servo":
            channel = action.get("channel")
            deg_min = action.get("deg_min")
            deg_max = action.get("deg_max")
            duration_s = action.get("duration_s", 5)
            step_hold_s = action.get("step_hold_s", 0.5)
            speed = action.get("speed", "normal")
            if isinstance(channel, int) and isinstance(deg_min, int) and isinstance(deg_max, int) and isinstance(duration_s, (int, float)) and isinstance(step_hold_s, (int, float)) and speed in {"slow", "normal", "fast"} and 12 <= channel <= 15 and 0 <= deg_min <= 180 and 0 <= deg_max <= 180 and duration_s > 0 and step_hold_s > 0:
                valid_actions.append({"intent": "oscillate_servo", "channel": channel, "deg_min": deg_min, "deg_max": deg_max, "duration_s": duration_s, "step_hold_s": step_hold_s, "speed": speed})

    return valid_actions


def enrich_actions_from_user_text(user_input: str, actions: list[dict]) -> list[dict]:
    text = user_input.lower()

    if "scuot" in text or "oscill" in text:
        duration_s = 10 if ("10" in text or "dieci" in text) else 20 if ("20" in text or "venti" in text) else 5
        step_hold_s = 0.25 if ("molto veloce" in text or "rapid" in text) else 0.4 if ("veloce" in text or "fast" in text) else 0.8
        return [{
            "intent": "oscillate_servo",
            "channel": 14,
            "deg_min": 35,
            "deg_max": 145,
            "duration_s": duration_s,
            "step_hold_s": step_hold_s,
            "speed": "fast" if ("veloce" in text or "fast" in text or "rapid" in text) else "normal"
        }]

    return actions


def execute_actions(actions):
    for action in actions:
        try:
            result = send_intent(action)
            logger.info("CORE ACTION -> %s | result=%s", action, result)
            time.sleep(0.05)
        except Exception as e:
            logger.error("Errore azione %s: %s", action, e)


def play_audio():
    try:
        result = send_intent({"intent": "play_audio"})
        logger.info("CORE AUDIO -> %s", result)
    except Exception as e:
        logger.error("Errore comando play_audio: %s", e)


def parse_model_json(content: str) -> dict:
    try:
        data = json.loads(content)
        if not isinstance(data, dict):
            raise ValueError("JSON root non è un oggetto")
        return data
    except Exception as exc:
        logger.error("JSON modello non valido: %s | content=%r", exc, content)
        return {"speech": "Errore interno di formattazione.", "actions": []}


def generate_tts_async(speech: str):
    def worker():
        if client is None:
            logger.error("OpenAI client non configurato: API key mancante")
            return

        try:
            audio_res = client.audio.speech.create(
                model=GPTE_TTS_MODEL,
                voice=GPTE_TTS_VOICE,
                input=speech
            )

            temp_path = PROJECT_DIR / "risposta.tmp.mp3"
            final_path = PROJECT_DIR / "risposta.mp3"

            with open(temp_path, "wb") as f:
                f.write(audio_res.read())

            os.replace(temp_path, final_path)
            play_audio()

        except Exception as e:
            logger.exception("Errore TTS/audio")

    threading.Thread(target=worker, daemon=True).start()


def generate_reply(user_input: str) -> dict:
    global messages

    if client is None:
        return {"speech": "La chiave OpenAI non è configurata.", "actions": []}

    enriched_input = user_input
    if any(x in user_input.lower() for x in ["wifi", "connessione", "segnale", "stato"]):
        enriched_input += f"\n\nDati Wi-Fi attuali: {get_wifi_status()}"

    messages[0] = {"role": "system", "content": build_system_prompt()}
    messages.append({"role": "user", "content": enriched_input})
    trim_messages(max_turns=10)

    try:
        completion = client.chat.completions.create(
            model=GPTE_MODEL,
            messages=messages,
            response_format={"type": "json_object"},
            timeout=25,
        )
        content = completion.choices[0].message.content or ""
    except Exception as e:
        logger.exception("Errore OpenAI chat completion")
        return {"speech": "Errore di connessione al modulo cognitivo.", "actions": []}

    data = parse_model_json(content)

    speech = data.get("speech", "Errore interno.")
    if not isinstance(speech, str) or not speech.strip():
        speech = "Errore interno."

    actions = normalize_actions(data.get("actions", []))
    actions = enrich_actions_from_user_text(user_input, actions)

    messages.append({"role": "assistant", "content": json.dumps({"speech": speech, "actions": actions}, ensure_ascii=False)})
    trim_messages(max_turns=10)

    execute_actions(actions)
    generate_tts_async(speech)

    return {"speech": speech, "actions": actions}


async def handle_brain_client(reader, writer) -> None:
    peer = writer.get_extra_info("peername")
    logger.info("Brain client connesso: %s", peer)

    try:
        while True:
            line = await reader.readline()
            if not line:
                break

            try:
                msg = json.loads(line.decode("utf-8").strip())
            except Exception:
                response = {"ok": False, "error": "invalid_json"}
                writer.write((json.dumps(response) + "\n").encode("utf-8"))
                await writer.drain()
                continue

            if msg.get("token") != CORE_TOKEN:
                response = {"ok": False, "error": "unauthorized"}
                writer.write((json.dumps(response) + "\n").encode("utf-8"))
                await writer.drain()
                continue

            action = msg.get("action")

            if action == "ask":
                text = msg.get("text", "").strip()
                if not text:
                    response = {"ok": False, "error": "empty_text"}
                else:
                    try:
                        result = await asyncio.to_thread(generate_reply, text)
                        response = {"ok": True, "reply": result["speech"], "actions": result["actions"]}
                    except Exception as e:
                        logger.exception("Errore generate_reply")
                        response = {"ok": False, "error": str(e)}

            elif action == "health":
                response = {
                    "ok": True,
                    "service": "gpte-brain",
                    "esp32_status": get_esp32_status(),
                    "model": GPTE_MODEL,
                    "tts": GPTE_TTS_MODEL,
                }
            else:
                response = {"ok": False, "error": "unknown_action"}

            writer.write((json.dumps(response, ensure_ascii=False) + "\n").encode("utf-8"))
            await writer.drain()

    finally:
        writer.close()
        await writer.wait_closed()
        logger.info("Brain client disconnesso: %s", peer)


async def main():
    server = await asyncio.start_server(handle_brain_client, BRAIN_HOST, BRAIN_PORT)
    logger.info("Brain TCP server attivo su %s", server.sockets[0].getsockname())

    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    asyncio.run(main())
PY

# ------------------------------------------------------------
# 7. Systemd services
# ------------------------------------------------------------
echo "[7/9] Creo servizi systemd..."

SHUTDOWN_BIN="$(command -v shutdown || echo /usr/sbin/shutdown)"
grep -q '^GPTE_SHUTDOWN_BIN=' "$PROJECT_DIR/.env" || echo "GPTE_SHUTDOWN_BIN=$SHUTDOWN_BIN" >> "$PROJECT_DIR/.env"

cat > "$PROJECT_DIR/gpte-core.service" <<EOF
[Unit]
Description=GPT-E Core Serial State Manager
After=network.target
Requires=network.target

[Service]
User=$USER
WorkingDirectory=$PROJECT_DIR
EnvironmentFile=$PROJECT_DIR/.env
ExecStart=$PROJECT_DIR/env/bin/python -m app.main_core
Restart=always
RestartSec=2
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

sudo cp "$PROJECT_DIR/gpte-core.service" "/etc/systemd/system/$SERVICE_CORE"

sudo tee "/etc/systemd/system/$SERVICE_BRAIN" > /dev/null <<EOF
[Unit]
Description=Servizio Brain GPT-E
After=network.target gpte-core.service
Requires=gpte-core.service

[Service]
User=$USER
WorkingDirectory=$PROJECT_DIR
EnvironmentFile=$PROJECT_DIR/.env
ExecStart=$PROJECT_DIR/env/bin/python $PROJECT_DIR/brain_service.py
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

sudo tee "/etc/systemd/system/$SERVICE_CONTROL" > /dev/null <<EOF
[Unit]
Description=Servizio Controllo Telecomando GPT-E
After=network.target gpte-core.service
Requires=gpte-core.service

[Service]
User=$USER
WorkingDirectory=$PROJECT_DIR
EnvironmentFile=$PROJECT_DIR/.env
ExecStart=$PROJECT_DIR/env/bin/python $PROJECT_DIR/control_service.py
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

echo "$USER ALL=(root) NOPASSWD: $SHUTDOWN_BIN -h now" | sudo tee /etc/sudoers.d/gpte-shutdown > /dev/null
sudo chmod 440 /etc/sudoers.d/gpte-shutdown
sudo visudo -cf /etc/sudoers.d/gpte-shutdown

# ------------------------------------------------------------
# 8. Abilita servizi
# ------------------------------------------------------------
echo "[8/9] Abilito e avvio servizi..."
sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_CORE"
sudo systemctl enable "$SERVICE_BRAIN"
sudo systemctl enable "$SERVICE_CONTROL"
sudo systemctl restart "$SERVICE_CORE"
sleep 2
sudo systemctl restart "$SERVICE_BRAIN"
sudo systemctl restart "$SERVICE_CONTROL"

# ------------------------------------------------------------
# 9. Test helper
# ------------------------------------------------------------
echo "[9/9] Creo script test helper..."

cat > "$PROJECT_DIR/test_status.py" <<'PY'
import json
import urllib.request

from core_client import get_state
from brain_client import brain_health

print("CORE:", get_state())
print("BRAIN:", brain_health())

try:
    with urllib.request.urlopen("http://127.0.0.1:5000/health", timeout=5) as response:
        print("CONTROL:", json.loads(response.read().decode("utf-8")))
except Exception as exc:
    print("CONTROL:", {"ok": False, "error": str(exc)})
PY

cat > "$PROJECT_DIR/test_servo.py" <<'PY'
import time
from core_client import send_intent

for ch in [12, 13, 14, 15]:
    print("CAN", ch)
    print(send_intent({"intent": "move_servo", "channel": ch, "deg": 60, "speed": "normal"}))
    time.sleep(1)
    print(send_intent({"intent": "move_servo", "channel": ch, "deg": 120, "speed": "normal"}))
    time.sleep(1)
    print(send_intent({"intent": "move_servo", "channel": ch, "deg": 90, "speed": "normal"}))
    time.sleep(1)
PY

cat > "$PROJECT_DIR/test_led.py" <<'PY'
import time
from core_client import send_intent

for color in ["red", "green", "blue", "white", "off"]:
    brightness = 0 if color == "off" else 100
    print(color, send_intent({"intent": "set_led", "color": color, "brightness": brightness}))
    time.sleep(2)
PY

cat > "$PROJECT_DIR/test_brain.py" <<'PY'
from brain_client import ask_brain

tests = [
    "Dimmi lo stato del sistema",
    "Accendi gli occhi blu",
    "Guarda a destra lentamente",
    "Scuoti la testa per dieci secondi",
]

for t in tests:
    print("\nTU:", t)
    print("GPT-E:", ask_brain(t))
PY

cat > "$PROJECT_DIR/gpte_help.sh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$HOME/GPT-E"
ENV_FILE="$PROJECT_DIR/.env"

service_state() {
    local service="$1"
    if systemctl is-active --quiet "$service"; then
        echo "OK"
    else
        echo "NON ATTIVO"
    fi
}

env_value() {
    local key="$1"
    if [ -f "$ENV_FILE" ]; then
        grep "^$key=" "$ENV_FILE" | head -n 1 | cut -d= -f2- || true
    fi
}

openai_key="$(env_value OPENAI_API_KEY)"
robot_ip="$(hostname -I 2>/dev/null | awk '{ print $1 }')"

echo ""
echo "================ GPT-E: COSA FARE ADESSO ================"
echo ""
echo "Stato servizi:"
echo "  Core UART/ESP32:       $(service_state gpte-core.service)"
echo "  Brain AI/TTS:          $(service_state gpte.service)"
echo "  Telecomando porta 5000: $(service_state gpte-control.service)"
echo ""

if [ -n "$robot_ip" ]; then
    echo "IP Raspberry rilevato: $robot_ip"
    echo "Il telecomando deve puntare all'IP configurato nel firmware."
    echo ""
fi

if [ -z "$openai_key" ] || [ "$openai_key" = "INSERISCI_LA_TUA_CHIAVE_OPENAI" ]; then
    echo "Manca OPENAI_API_KEY."
    echo "1. Apri il file:"
    echo "   nano $ENV_FILE"
    echo "2. Sostituisci OPENAI_API_KEY=INSERISCI_LA_TUA_CHIAVE_OPENAI"
    echo "3. Riavvia il brain:"
    echo "   sudo systemctl restart gpte.service"
else
    echo "OPENAI_API_KEY configurata."
fi

echo ""
echo "Comandi utili:"
echo "  Stato completo: cd $PROJECT_DIR && source env/bin/activate && python test_status.py"
echo "  Chat manuale:   cd $PROJECT_DIR && source env/bin/activate && python chat_cli.py"
echo "  Log live:       journalctl -u gpte-core.service -u gpte.service -u gpte-control.service -f"
echo "  Riavvia tutto:  sudo systemctl restart gpte-core.service gpte.service gpte-control.service"
echo ""
echo "Dal telecomando:"
echo "  Power ON  -> accensione hardware robot"
echo "  Porta 5000 -> controllo pronto/shutdown Raspberry"
echo "  Porta 5001 -> audio TTS verso ESP32 testa"
echo ""
echo "=========================================================="
SH
chmod +x "$PROJECT_DIR/gpte_help.sh"

cat > "$PROJECT_DIR/LEGGIMI_PRIMA.txt" <<TXT
GPT-E installato.

Per capire subito cosa fare:

cd ~/GPT-E
./gpte_help.sh

Il token interno GPTE_CORE_TOKEN e' stato generato automaticamente.
Di solito devi solo inserire OPENAI_API_KEY se non l'hai messa durante l'installazione.
TXT

echo ""
echo "============================================================"
echo "INSTALLAZIONE SISTEMA GPT-E COMPLETATA"
echo "============================================================"
echo "1) Guida rapida:"
echo "   cd $PROJECT_DIR && ./gpte_help.sh"
echo ""
echo "2) Se durante l'installazione hai premuto INVIO, inserisci solo OPENAI_API_KEY nel file .env:"
echo "   nano $PROJECT_DIR/.env"
echo ""
echo "3) Riavvia:"
echo "   sudo systemctl restart gpte-core.service gpte.service gpte-control.service"
echo ""
echo "4) Verifica:"
echo "   cd $PROJECT_DIR && source env/bin/activate && python test_status.py"
echo ""
echo "5) Chat:"
echo "   cd $PROJECT_DIR && source env/bin/activate && python chat_cli.py"
echo ""
echo "Log:"
echo "   journalctl -u gpte-core.service -f"
echo "   journalctl -u gpte.service -f"
echo "   journalctl -u gpte-control.service -f"
echo "============================================================"
