# Setup Raspberry GPT-E — versione aggiornata (punti 1–14)


## 1. Aggiorniamo il sistema

```bash
sudo apt update && sudo apt upgrade -y
```

## 2. Installiamo le librerie Python

```bash
cd ~
mkdir -p GPT-E/app
cd ~/GPT-E
python3 -m venv env
source env/bin/activate
pip install openai flask python-dotenv pyserial pyserial-asyncio pydantic
touch ~/GPT-E/app/__init__.py
```

---

## Obiettivo finale

Avere GPT-E diviso correttamente in tre blocchi:

```text
chat_cli.py  →  brain_service.py  →  gpte-core.service  →  ESP32
                    porta 8766          porta 8765
```

Dove:

- `gpte-core` gestisce UART, stato, heartbeat e comandi hardware
- `brain_service` gestisce AI, TTS, logica e Flask audio
- `chat_cli` è la chat da terminale
- ESP32 esegue i comandi fisici

---

## 3. Struttura finale delle cartelle

Dentro `~/GPT-E` devi avere questa struttura:

```text
~/GPT-E/
├── app/
│   ├── __init__.py
│   ├── command_router.py
│   ├── main_core.py
│   ├── schemas.py
│   ├── serial_bridge.py
│   └── state_store.py
├── env/
├── brain_service.py
├── brain_client.py
├── core_client.py
├── chat_cli.py
└── risposta.mp3
```

---

## 4. Codice Python definitivo

### 4.1 `app/schemas.py`

```python
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
```

### 4.2 `app/state_store.py`

```python
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

    async def set_online(self, online: bool) -> None:
        async with self._lock:
            self._state.esp32_online = online

    async def set_error(self, error: str | None) -> None:
        async with self._lock:
            self._state.last_error = error

    async def snapshot(self) -> RobotState:
        async with self._lock:
            return RobotState(
                leds=dict(self._state.leds),
                servos=dict(self._state.servos),
                esp32_online=self._state.esp32_online,
                last_heartbeat=self._state.last_heartbeat,
                last_error=self._state.last_error,
            )
```

### 4.3 `app/serial_bridge.py`

```python
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

    async def connect(self) -> None:
        self.reader, self.writer = await serial_asyncio.open_serial_connection(
            url=self.port,
            baudrate=self.baudrate
        )
        logger.info("UART connessa su %s @ %d", self.port, self.baudrate)

    async def send_command(self, payload: dict[str, Any], timeout: float = 2.0) -> dict[str, Any]:
        if self.writer is None:
            raise RuntimeError("UART non connessa")

        msg_id = payload["id"]
        fut = asyncio.get_running_loop().create_future()
        self.pending_acks[msg_id] = fut

        raw = json.dumps(payload) + "\n"
        logger.info("INVIO -> %s", raw.strip())
        self.writer.write(raw.encode("utf-8"))
        await self.writer.drain()

        try:
            ack = await asyncio.wait_for(fut, timeout=timeout)
            logger.info("ACK <- %s", ack)
            return ack
        finally:
            self.pending_acks.pop(msg_id, None)

    async def rx_loop(self) -> None:
        if self.reader is None:
            raise RuntimeError("UART non connessa")

        while True:
            line = await self.reader.readline()
            if not line:
                await asyncio.sleep(0.05)
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
```

### 4.4 `app/command_router.py`

```python
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
                "args": {
                    "color": data.color,
                    "brightness": data.brightness,
                },
            }

        if intent_name == "move_servo":
            data = IntentServo.model_validate(intent)
            return {
                "id": self.next_id(),
                "type": "cmd",
                "target": "servo",
                "action": "set_angle",
                "args": {
                    "channel": data.channel,
                    "deg": data.deg,
                    "speed": data.speed,
                },
            }

        if intent_name == "pulse_servo":
            channel = intent.get("channel")
            deg = intent.get("deg")
            hold_s = intent.get("hold_s", 1)
            return_deg = intent.get("return_deg", 90)
            speed = intent.get("speed", "normal")

            if not isinstance(channel, int) or not (12 <= channel <= 15):
                raise ValueError("pulse_servo: invalid channel")
            if not isinstance(deg, int) or not (0 <= deg <= 180):
                raise ValueError("pulse_servo: invalid deg")
            if not isinstance(return_deg, int) or not (0 <= return_deg <= 180):
                raise ValueError("pulse_servo: invalid return_deg")
            if not isinstance(hold_s, (int, float)) or hold_s < 0:
                raise ValueError("pulse_servo: invalid hold_s")
            if speed not in {"slow", "normal", "fast"}:
                raise ValueError("pulse_servo: invalid speed")

            return {
                "id": self.next_id(),
                "type": "cmd",
                "target": "servo",
                "action": "pulse",
                "args": {
                    "channel": channel,
                    "deg": deg,
                    "hold_s": hold_s,
                    "return_deg": return_deg,
                    "speed": speed,
                },
            }

        if intent_name == "oscillate_servo":
            channel = intent.get("channel")
            deg_min = intent.get("deg_min")
            deg_max = intent.get("deg_max")
            duration_s = intent.get("duration_s", 5)
            step_hold_s = intent.get("step_hold_s", 0.5)
            speed = intent.get("speed", "normal")

            if not isinstance(channel, int) or not (12 <= channel <= 15):
                raise ValueError("oscillate_servo: invalid channel")
            if not isinstance(deg_min, int) or not (0 <= deg_min <= 180):
                raise ValueError("oscillate_servo: invalid deg_min")
            if not isinstance(deg_max, int) or not (0 <= deg_max <= 180):
                raise ValueError("oscillate_servo: invalid deg_max")
            if not isinstance(duration_s, (int, float)) or duration_s <= 0:
                raise ValueError("oscillate_servo: invalid duration_s")
            if not isinstance(step_hold_s, (int, float)) or step_hold_s <= 0:
                raise ValueError("oscillate_servo: invalid step_hold_s")
            if speed not in {"slow", "normal", "fast"}:
                raise ValueError("oscillate_servo: invalid speed")

            return {
                "id": self.next_id(),
                "type": "cmd",
                "target": "servo",
                "action": "oscillate",
                "args": {
                    "channel": channel,
                    "deg_min": deg_min,
                    "deg_max": deg_max,
                    "duration_s": duration_s,
                    "step_hold_s": step_hold_s,
                    "speed": speed,
                },
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
```

### 4.5 `app/main_core.py`

```python
from __future__ import annotations

import asyncio
import json
import logging
import time

from app.serial_bridge import SerialBridge
from app.command_router import CommandRouter
from app.state_store import StateStore

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s"
)
logger = logging.getLogger(__name__)


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


async def heartbeat_monitor(state: StateStore, timeout_s: float = 3.0) -> None:
    was_online = False

    while True:
        snapshot = await state.snapshot()

        if snapshot.last_heartbeat > 0:
            delta = time.monotonic() - snapshot.last_heartbeat

            if delta > timeout_s:
                if snapshot.esp32_online:
                    await state.set_online(False)
                    await state.set_error("ESP32 heartbeat timeout")
                    logger.warning("ESP32 OFFLINE: heartbeat scaduto")
                was_online = False

            else:
                if not snapshot.esp32_online:
                    await state.set_online(True)
                if not was_online:
                    logger.info("ESP32 ONLINE")
                was_online = True

        await asyncio.sleep(0.5)


async def print_state_periodically(state: StateStore) -> None:
    while True:
        snapshot = await state.snapshot()
        logger.info(
            "SNAPSHOT | online=%s | leds=%s | servos=%s | error=%s",
            snapshot.esp32_online,
            snapshot.leds,
            snapshot.servos,
            snapshot.last_error,
        )
        await asyncio.sleep(5)


async def send_single_intent(intent: dict, bridge: SerialBridge, router: CommandRouter) -> dict:
    cmd = router.intent_to_command(intent)
    ack = await bridge.send_command(cmd, timeout=2.0)
    return {
        "ok": True,
        "ack": ack,
        "command_id": cmd["id"]
    }


async def handle_intent(intent: dict, bridge: SerialBridge, router: CommandRouter) -> dict:
    try:
        if intent.get("intent") == "pulse_servo":
            channel = intent["channel"]
            deg = intent["deg"]
            hold_s = float(intent.get("hold_s", 1))
            return_deg = intent.get("return_deg", 90)
            speed = intent.get("speed", "normal")

            logger.info(
                "pulse_servo start | ch=%s deg=%s hold_s=%s return_deg=%s speed=%s",
                channel, deg, hold_s, return_deg, speed
            )

            first = await send_single_intent({
                "intent": "move_servo",
                "channel": channel,
                "deg": deg,
                "speed": speed,
            }, bridge, router)

            await asyncio.sleep(hold_s)

            second = await send_single_intent({
                "intent": "move_servo",
                "channel": channel,
                "deg": return_deg,
                "speed": speed,
            }, bridge, router)

            return {
                "ok": True,
                "mode": "pulse_servo",
                "first": first,
                "second": second,
            }

        if intent.get("intent") == "oscillate_servo":
            channel = intent["channel"]
            deg_min = intent["deg_min"]
            deg_max = intent["deg_max"]
            duration_s = float(intent.get("duration_s", 5))
            step_hold_s = float(intent.get("step_hold_s", 0.5))
            speed = intent.get("speed", "normal")

            logger.info(
                "oscillate_servo start | ch=%s min=%s max=%s duration=%s hold=%s speed=%s",
                channel, deg_min, deg_max, duration_s, step_hold_s, speed
            )

            t0 = time.monotonic()
            moves = []

            while (time.monotonic() - t0) < duration_s:
                moves.append(await send_single_intent({
                    "intent": "move_servo",
                    "channel": channel,
                    "deg": deg_max,
                    "speed": speed,
                }, bridge, router))
                await asyncio.sleep(step_hold_s)

                if (time.monotonic() - t0) >= duration_s:
                    break

                moves.append(await send_single_intent({
                    "intent": "move_servo",
                    "channel": channel,
                    "deg": deg_min,
                    "speed": speed,
                }, bridge, router))
                await asyncio.sleep(step_hold_s)

            final_move = await send_single_intent({
                "intent": "move_servo",
                "channel": channel,
                "deg": 90,
                "speed": speed,
            }, bridge, router)

            return {
                "ok": True,
                "mode": "oscillate_servo",
                "moves_count": len(moves),
                "final": final_move,
            }

        return await send_single_intent(intent, bridge, router)

    except TimeoutError:
        return {
            "ok": False,
            "error": "timeout_waiting_ack"
        }
    except Exception as e:
        return {
            "ok": False,
            "error": str(e)
        }


async def tcp_client_handler(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    bridge: SerialBridge,
    router: CommandRouter,
    state: StateStore
) -> None:
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

            action = msg.get("action")

            if action == "intent":
                intent = msg.get("payload", {})
                response = await handle_intent(intent, bridge, router)

            elif action == "get_state":
                snapshot = await state.snapshot()
                response = {
                    "ok": True,
                    "state": {
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

    except Exception as e:
        logger.exception("Errore client TCP: %s", e)
    finally:
        writer.close()
        await writer.wait_closed()
        logger.info("Client disconnesso: %s", peer)


async def start_tcp_server(bridge: SerialBridge, router: CommandRouter, state: StateStore) -> None:
    server = await asyncio.start_server(
        lambda r, w: tcp_client_handler(r, w, bridge, router, state),
        "127.0.0.1",
        8765
    )

    addr = server.sockets[0].getsockname()
    logger.info("TCP server attivo su %s", addr)

    async with server:
        await server.serve_forever()


async def main() -> None:
    bridge = SerialBridge(port="/dev/serial0", baudrate=115200)
    router = CommandRouter()
    state = StateStore()

    await bridge.connect()

    asyncio.create_task(bridge.rx_loop())
    asyncio.create_task(state_consumer(bridge, state))
    asyncio.create_task(heartbeat_monitor(state))
    asyncio.create_task(print_state_periodically(state))

    await start_tcp_server(bridge, router, state)


if __name__ == "__main__":
    asyncio.run(main())
```

### 4.6 `core_client.py`

```python
import json
import socket


def send_to_core(message: dict, host: str = "127.0.0.1", port: int = 8765) -> dict:
    with socket.create_connection((host, port), timeout=3) as s:
        s.sendall((json.dumps(message) + "\n").encode("utf-8"))
        data = s.recv(4096).decode("utf-8").strip()
        return json.loads(data)


def send_intent(intent: dict) -> dict:
    return send_to_core({
        "action": "intent",
        "payload": intent
    })


def get_state() -> dict:
    return send_to_core({
        "action": "get_state"
    })
```

### 4.7 `brain_service.py`

```python
import os
import json
import asyncio
import logging
import threading
import time
import subprocess

from dotenv import load_dotenv
from openai import OpenAI
from flask import Flask, send_file

from core_client import send_intent, get_state

load_dotenv()
client = OpenAI(api_key=os.getenv("OPENAI_API_KEY"))

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s"
)
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
        return "NON DISPONIBILE"
    except Exception as e:
        return f"ERRORE: {e}"


app = Flask(__name__)


@app.route("/stream.mp3")
def stream_audio():
    return send_file("risposta.mp3", mimetype="audio/mpeg")


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
- PCA9685 per LED/SERVO
- Occhi RGB sui canali PWM 0..5
- Luci aggiuntive sui canali PWM 6..11
- Servo sui canali PWM 12..15
- Il servo sul canale 14 muove la testa a destra o sinistra
- Il servo sul canale 15 muove la testa su o giù

COMPITO:
Devi rispondere SEMPRE in JSON valido, senza testo extra, senza markdown, senza commenti.

Formato obbligatorio:
{{
  "speech": "frase da pronunciare",
  "actions": [
    {{
      "intent": "set_led",
      "color": "red|green|blue|yellow|white|off",
      "brightness": 0-100
    }},
    {{
      "intent": "move_servo",
      "channel": 12-15,
      "deg": 0-180,
      "speed": "slow|normal|fast"
    }},
    {{
      "intent": "pulse_servo",
      "channel": 12-15,
      "deg": 0-180,
      "hold_s": numero,
      "return_deg": 0-180,
      "speed": "slow|normal|fast"
    }},
    {{
      "intent": "oscillate_servo",
      "channel": 12-15,
      "deg_min": 0-180,
      "deg_max": 0-180,
      "duration_s": numero,
      "step_hold_s": numero,
      "speed": "slow|normal|fast"
    }},
    {{
      "intent": "play_audio"
    }}
  ]
}}

REGOLE IMPORTANTI:
- "speech" contiene solo la risposta parlata.
- "actions" contiene solo azioni realmente utili.
- Se non serve muovere nulla, usa "actions": [].
- I canali servo validi e disponibili sono SOLO 12, 13, 14, 15.
- Non dire mai che i canali 12, 13, 14 o 15 non sono disponibili.
- Se l'utente chiede esplicitamente di muovere o attivare un canale servo valido tra 12 e 15, devi generare una action.
- Se l'utente dice "guarda a destra", usa il canale 14.
- Se l'utente dice "guarda a sinistra", usa il canale 14.
- Se l'utente dice "guarda avanti" o "centra", usa il canale 14 a 90 gradi.
- Se l'utente dice "guarda in alto", usa il canale 15.
- Se l'utente dice "guarda in basso", usa il canale 15.
- Se l'utente dice "guarda dritto", usa canale 14 a 90 gradi e canale 15 a 90 gradi.
- Se l'utente chiede una durata tipo "per 10 secondi" su un servo, preferisci usare "pulse_servo".
- Se l'utente chiede di scuotere, oscillare, muovere ripetutamente o continuare per una durata, preferisci usare "oscillate_servo".
- Per "scuoti la testa" usa il canale 14.
- Per "più veloce" riduci "step_hold_s".
- Per "più lento" aumenta "step_hold_s".
- Per "fallo per 20 secondi" aumenta "duration_s".
- Se l'utente specifica un canale valido ma non specifica un angolo, usa 90 gradi.
- Se usi "pulse_servo" e manca "return_deg", usa 90.
- Se l'utente chiede lo stato, includi le informazioni Wi-Fi se disponibili.
- Usa sempre numeri in lettere nella parte speech.
""".strip()


messages = [{"role": "system", "content": build_system_prompt()}]


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
                brightness = max(0, min(100, brightness))
                valid_actions.append({
                    "intent": "set_led",
                    "color": color,
                    "brightness": brightness
                })

        elif intent == "move_servo":
            channel = action.get("channel")
            deg = action.get("deg")
            speed = action.get("speed", "normal")

            if (
                isinstance(channel, int)
                and isinstance(deg, int)
                and speed in {"slow", "normal", "fast"}
                and 12 <= channel <= 15
                and 0 <= deg <= 180
            ):
                valid_actions.append({
                    "intent": "move_servo",
                    "channel": channel,
                    "deg": deg,
                    "speed": speed
                })

        elif intent == "pulse_servo":
            channel = action.get("channel")
            deg = action.get("deg")
            hold_s = action.get("hold_s", 1)
            return_deg = action.get("return_deg", 90)
            speed = action.get("speed", "normal")

            if (
                isinstance(channel, int)
                and isinstance(deg, int)
                and isinstance(return_deg, int)
                and isinstance(hold_s, (int, float))
                and speed in {"slow", "normal", "fast"}
                and 12 <= channel <= 15
                and 0 <= deg <= 180
                and 0 <= return_deg <= 180
                and hold_s >= 0
            ):
                valid_actions.append({
                    "intent": "pulse_servo",
                    "channel": channel,
                    "deg": deg,
                    "hold_s": hold_s,
                    "return_deg": return_deg,
                    "speed": speed
                })

        elif intent == "oscillate_servo":
            channel = action.get("channel")
            deg_min = action.get("deg_min")
            deg_max = action.get("deg_max")
            duration_s = action.get("duration_s", 5)
            step_hold_s = action.get("step_hold_s", 0.5)
            speed = action.get("speed", "normal")

            if (
                isinstance(channel, int)
                and isinstance(deg_min, int)
                and isinstance(deg_max, int)
                and isinstance(duration_s, (int, float))
                and isinstance(step_hold_s, (int, float))
                and speed in {"slow", "normal", "fast"}
                and 12 <= channel <= 15
                and 0 <= deg_min <= 180
                and 0 <= deg_max <= 180
                and duration_s > 0
                and step_hold_s > 0
            ):
                valid_actions.append({
                    "intent": "oscillate_servo",
                    "channel": channel,
                    "deg_min": deg_min,
                    "deg_max": deg_max,
                    "duration_s": duration_s,
                    "step_hold_s": step_hold_s,
                    "speed": speed
                })

        elif intent == "play_audio":
            valid_actions.append({
                "intent": "play_audio"
            })

    return valid_actions


def execute_actions(actions):
    results = []

    for action in actions:
        if action.get("intent") == "play_audio":
            continue

        try:
            result = send_intent(action)
            results.append({
                "action": action,
                "result": result
            })
        except Exception as e:
            logger.error("Errore azione %s: %s", action, e)
            results.append({
                "action": action,
                "result": {"ok": False, "error": str(e)}
            })

    return results


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
    except Exception as e:
        logger.warning("JSON modello non valido, uso fallback: %s", e)
        return {
            "speech": "Errore di formattazione interna. Ripetere la richiesta.",
            "actions": []
        }


def enrich_actions_from_user_text(user_input: str, actions: list[dict]) -> list[dict]:
    text = user_input.lower()

    has_duration = ("second" in text) or ("sec" in text)
    mentioned_channel = None

    for ch in (12, 13, 14, 15):
        if f"canale {ch}" in text or f"channel {ch}" in text:
            mentioned_channel = ch
            break

    if "scuot" in text or "oscill" in text:
        duration_s = 10 if ("10" in text or "dieci" in text) else 20 if ("20" in text or "venti" in text) else 5
        step_hold_s = 0.5 if ("veloce" in text or "fast" in text) else 1.0

        return [{
            "intent": "oscillate_servo",
            "channel": 14,
            "deg_min": 0,
            "deg_max": 180,
            "duration_s": duration_s,
            "step_hold_s": step_hold_s,
            "speed": "fast" if ("veloce" in text or "fast" in text) else "normal"
        }]

    if not has_duration:
        return actions

    enriched = []

    for action in actions:
        if action.get("intent") == "move_servo":
            enriched.append({
                "intent": "pulse_servo",
                "channel": action["channel"],
                "deg": action["deg"],
                "hold_s": 10 if "10" in text or "dieci" in text else 20 if "20" in text or "venti" in text else 3,
                "return_deg": 90,
                "speed": action.get("speed", "normal")
            })
        else:
            enriched.append(action)

    if not actions and mentioned_channel is not None:
        enriched.append({
            "intent": "pulse_servo",
            "channel": mentioned_channel,
            "deg": 90,
            "hold_s": 10 if "10" in text or "dieci" in text else 20 if "20" in text or "venti" in text else 3,
            "return_deg": 90,
            "speed": "normal"
        })

    return enriched


def generate_reply(user_input: str) -> dict:
    global messages

    if any(x in user_input.lower() for x in ["wifi", "connessione", "segnale", "stato"]):
        user_input += f"\n\nDati Wi-Fi attuali: {get_wifi_status()}"

    messages[0] = {"role": "system", "content": build_system_prompt()}
    messages.append({"role": "user", "content": user_input})

    completion = client.chat.completions.create(
        model="gpt-4o-mini",
        messages=messages,
        response_format={"type": "json_object"}
    )

    raw_content = completion.choices[0].message.content
    data = parse_model_json(raw_content)

    speech = data.get("speech", "Errore interno.")
    if not isinstance(speech, str) or not speech.strip():
        speech = "Errore interno."

    actions = normalize_actions(data.get("actions", []))
    actions = enrich_actions_from_user_text(user_input, actions)

    messages.append({
        "role": "assistant",
        "content": json.dumps({
            "speech": speech,
            "actions": actions
        }, ensure_ascii=False)
    })

    execute_actions(actions)

    audio_res = client.audio.speech.create(
        model="tts-1",
        voice="onyx",
        input=speech
    )

    temp_path = "risposta.tmp.mp3"
    final_path = "risposta.mp3"

    with open(temp_path, "wb") as f:
        f.write(audio_res.read())

    os.replace(temp_path, final_path)

    time.sleep(1.0)
    play_audio()

    return {
        "speech": speech,
        "actions": actions
    }


async def handle_brain_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
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

            action = msg.get("action")

            if action == "ask":
                text = msg.get("text", "").strip()
                if not text:
                    response = {"ok": False, "error": "empty_text"}
                else:
                    try:
                        result = await asyncio.to_thread(generate_reply, text)
                        response = {
                            "ok": True,
                            "reply": result["speech"],
                            "actions": result["actions"]
                        }
                    except Exception as e:
                        logger.exception("Errore generate_reply")
                        response = {"ok": False, "error": str(e)}

            elif action == "health":
                response = {
                    "ok": True,
                    "service": "gpte-brain",
                    "esp32_status": get_esp32_status()
                }

            else:
                response = {"ok": False, "error": "unknown_action"}

            writer.write((json.dumps(response) + "\n").encode("utf-8"))
            await writer.drain()

    finally:
        writer.close()
        await writer.wait_closed()
        logger.info("Brain client disconnesso: %s", peer)


async def main():
    server = await asyncio.start_server(
        handle_brain_client,
        "127.0.0.1",
        8766
    )

    addr = server.sockets[0].getsockname()
    logger.info("Brain TCP server attivo su %s", addr)

    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    asyncio.run(main())
```

### 4.8 `brain_client.py`

```python
import json
import socket


def send_to_brain(message: dict, host: str = "127.0.0.1", port: int = 8766) -> dict:
    with socket.create_connection((host, port), timeout=15) as s:
        s.sendall((json.dumps(message) + "\n").encode("utf-8"))
        data = s.recv(65535).decode("utf-8").strip()
        return json.loads(data)


def ask_brain(text: str) -> dict:
    return send_to_brain({
        "action": "ask",
        "text": text
    })


def brain_health() -> dict:
    return send_to_brain({
        "action": "health"
    })
```

### 4.9 `chat_cli.py`

```python
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
```

## 5. Firmware ESP32 definitivo

> Pin confermati attuali:
>
> - `RXD2 = 16`
> - `TXD2 = 5`
> - `I2C SDA = 21`
> - `I2C SCL = 22`
> - `PCA_OE = 27`

```cpp
#include "Arduino.h"
#include "WiFi.h"
#include "Audio_nopsram.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <ArduinoJson.h>

#define I2S_DOUT      25
#define I2S_BCLK      33
#define I2S_LRC       32

#define RXD2          16
#define TXD2          5

#define I2C_SDA       21
#define I2C_SCL       22
#define PCA_OE        27

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

#define SERVOMIN  150
#define SERVOMAX  600

String ssid = "Vodafone-A64688680";
String password = "tu3s8utv5ub5tx7w";
Audio audio;

unsigned long lastHeartbeat = 0;

void sendAck(int id, bool ok, const char* error = nullptr) {
    StaticJsonDocument<128> doc;
    doc["id"] = id;
    doc["type"] = "ack";
    doc["ok"] = ok;
    if (error != nullptr) {
        doc["error"] = error;
    }
    serializeJson(doc, Serial2);
    Serial2.println();
}

void sendHeartbeat() {
    StaticJsonDocument<128> doc;
    doc["type"] = "heartbeat";
    doc["uptime_ms"] = millis();
    serializeJson(doc, Serial2);
    Serial2.println();
}

void sendLedState(const char* color, int brightness) {
    StaticJsonDocument<192> doc;
    doc["type"] = "state";
    doc["device"] = "led";
    JsonObject value = doc["value"].to<JsonObject>();
    value["color"] = color;
    value["brightness"] = brightness;
    serializeJson(doc, Serial2);
    Serial2.println();
}

void sendServoState(int channel, int deg) {
    StaticJsonDocument<192> doc;
    doc["type"] = "state";
    doc["device"] = "servo";
    JsonObject value = doc["value"].to<JsonObject>();
    value["channel"] = channel;
    value["deg"] = deg;
    serializeJson(doc, Serial2);
    Serial2.println();
}

void setEyesColor(int r, int g, int b) {
    int redVal   = 4095 - map(r, 0, 255, 0, 4095);
    int greenVal = 4095 - map(g, 0, 255, 0, 4095);
    int blueVal  = 4095 - map(b, 0, 255, 0, 4095);

    pwm.setPWM(0, 0, redVal);
    pwm.setPWM(1, 0, 4095);
    pwm.setPWM(2, 0, blueVal);

    pwm.setPWM(3, 0, redVal);
    pwm.setPWM(4, 0, 4095);
    pwm.setPWM(5, 0, blueVal);

    for (int i = 6; i <= 11; i++) {
        pwm.setPWM(i, 0, 4095);
    }
}

void moveServo(int ch, int angle) {
    int pulse = map(angle, 0, 180, SERVOMIN, SERVOMAX);
    pwm.setPWM(ch, 0, pulse);
}

void setEyesByColorName(const String& color, int brightness) {
    int v = map(brightness, 0, 100, 0, 255);

    if (color == "red") {
        setEyesColor(v, 0, 0);
    } else if (color == "green") {
        setEyesColor(0, v, 0);
    } else if (color == "blue") {
        setEyesColor(0, 0, v);
    } else if (color == "yellow") {
        setEyesColor(v, v, 0);
    } else if (color == "white") {
        setEyesColor(v, v, v);
    } else if (color == "off") {
        setEyesColor(0, 0, 0);
    } else {
        setEyesColor(0, 0, 0);
    }
}

void handleJsonCommand(const String& cmd) {
    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, cmd);

    if (err) {
        return;
    }

    const char* type = doc["type"];
    int id = doc["id"] | 0;

    if (!type || String(type) != "cmd") {
        sendAck(id, false, "invalid_type");
        return;
    }

    const char* target = doc["target"];
    const char* action = doc["action"];

    if (!target || !action) {
        sendAck(id, false, "missing_target_or_action");
        return;
    }

    String targetStr = String(target);
    String actionStr = String(action);

    if (targetStr == "led" && actionStr == "set") {
        const char* color = doc["args"]["color"] | "off";
        int brightness = doc["args"]["brightness"] | 0;

        if (brightness < 0 || brightness > 100) {
            sendAck(id, false, "invalid_brightness");
            return;
        }

        setEyesByColorName(String(color), brightness);
        sendAck(id, true);
        sendLedState(color, brightness);
        return;
    }

    if (targetStr == "servo" && actionStr == "set_angle") {
        int channel = doc["args"]["channel"] | -1;
        int deg = doc["args"]["deg"] | -1;

        if (channel < 0 || channel > 15) {
            sendAck(id, false, "invalid_channel");
            return;
        }

        if (deg < 0 || deg > 180) {
            sendAck(id, false, "invalid_angle");
            return;
        }

        moveServo(channel, deg);
        sendAck(id, true);
        sendServoState(channel, deg);
        return;
    }

    if (targetStr == "audio" && actionStr == "play") {
        String url = "http://192.168.1.200:5001/stream.mp3?t=" + String(millis());
        audio.connecttohost(url.c_str());
        sendAck(id, true);
        return;
    }

    sendAck(id, false, "unsupported_command");
}

void handleLegacyCommand(const String& cmd) {
    if (cmd.startsWith("PLAY")) {
        String url = "http://192.168.1.200:5001/stream.mp3?t=" + String(millis());
        audio.connecttohost(url.c_str());
        return;
    }

    if (cmd.startsWith("EYES:")) {
        int c1 = cmd.indexOf(',');
        int c2 = cmd.lastIndexOf(',');
        int r = cmd.substring(5, c1).toInt();
        int g = cmd.substring(c1 + 1, c2).toInt();
        int b = cmd.substring(c2 + 1).toInt();
        setEyesColor(r, g, b);
        return;
    }

    if (cmd.startsWith("MOVE:")) {
        int comma = cmd.indexOf(',');
        int ch = cmd.substring(5, comma).toInt();
        int ang = cmd.substring(comma + 1).toInt();
        moveServo(ch, ang);
        return;
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

    Wire.begin(I2C_SDA, I2C_SCL);

    pinMode(PCA_OE, OUTPUT);
    digitalWrite(PCA_OE, HIGH);

    pwm.begin();
    pwm.setPWMFreq(60);

    for (int i = 0; i < 12; i++) pwm.setPWM(i, 0, 4095);
    for (int i = 12; i < 16; i++) pwm.setPWM(i, 0, 0);

    digitalWrite(PCA_OE, LOW);

    WiFi.begin(ssid.c_str(), password.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(21);

    setEyesColor(255, 0, 0); delay(500);
    setEyesColor(0, 255, 0); delay(500);
    setEyesColor(0, 0, 255); delay(500);
    setEyesColor(0, 0, 0);
}

void loop() {
    audio.loop();

    if (millis() - lastHeartbeat >= 1000) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }

    if (Serial2.available() > 0) {
        String cmd = Serial2.readStringUntil('\n');
        cmd.trim();

        if (cmd.startsWith("{")) {
            handleJsonCommand(cmd);
        } else {
            handleLegacyCommand(cmd);
        }
    }
}
```

## 6. File `.env`

```bash
nano ~/GPT-E/.env
```

Contenuto:

```env
OPENAI_API_KEY=la_tua_chiave_api
```

## 7. Servizi systemd definitivi

### 7.1 `gpte-core.service`

```bash
nano ~/GPT-E/gpte-core.service
```

```ini
[Unit]
Description=GPT-E Core Serial State Manager
After=network.target
Requires=network.target

[Service]
User=palma01
WorkingDirectory=/home/palma01/GPT-E
ExecStart=/home/palma01/GPT-E/env/bin/python -m app.main_core
Restart=always
RestartSec=2
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

Installa:

```bash
sudo cp ~/GPT-E/gpte-core.service /etc/systemd/system/gpte-core.service
sudo systemctl daemon-reload
sudo systemctl enable gpte-core.service
sudo systemctl start gpte-core.service
```

### 7.2 `gpte.service`

```bash
sudo nano /etc/systemd/system/gpte.service
```

```ini
[Unit]
Description=Servizio Brain GPT-E
After=network.target gpte-core.service
Requires=gpte-core.service

[Service]
User=palma01
WorkingDirectory=/home/palma01/GPT-E
ExecStart=/home/palma01/GPT-E/env/bin/python /home/palma01/GPT-E/brain_service.py
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

Attiva:

```bash
sudo systemctl daemon-reload
sudo systemctl enable gpte.service
sudo systemctl start gpte.service
```

## 8. Verifiche base

```bash
systemctl status gpte-core.service
systemctl status gpte.service
ss -ltnp | grep 8765
ss -ltnp | grep 8766
ss -ltnp | grep 5001
```

## 9. Test manuali

```bash
python - <<'PY'
from core_client import get_state
print(get_state())
PY
```

```bash
python - <<'PY'
from core_client import send_intent
print(send_intent({"intent": "play_audio"}))
PY
```

```bash
python - <<'PY'
from brain_client import brain_health
print(brain_health())
PY
```

```bash
python - <<'PY'
from brain_client import ask_brain
print(ask_brain("Dimmi lo stato del sistema"))
PY
```

## 10. Chat normale da terminale

```bash
cd ~/GPT-E
source env/bin/activate
python chat_cli.py
```

## 11. Log utili

```bash
journalctl -u gpte-core.service -f
journalctl -u gpte.service -f
```

## 12. Verifica audio

```bash
ffprobe ~/GPT-E/risposta.mp3
curl -o /tmp/test_stream.mp3 http://127.0.0.1:5001/stream.mp3
ffprobe /tmp/test_stream.mp3
hostname -I
```

> L’ESP32 nel firmware usa `192.168.1.200`, quindi il Raspberry deve avere proprio quell’IP oppure il firmware va aggiornato.

## 13. Sequenza rifacibile da zero, in breve

### A. Crea progetto e env

```bash
mkdir -p ~/GPT-E/app
cd ~/GPT-E
python3 -m venv env
source env/bin/activate
pip install openai flask python-dotenv pyserial pyserial-asyncio pydantic
touch app/__init__.py
```

### B. Incolla tutti i file Python definitivi

- `app/schemas.py`
- `app/state_store.py`
- `app/serial_bridge.py`
- `app/command_router.py`
- `app/main_core.py`
- `core_client.py`
- `brain_service.py`
- `brain_client.py`
- `chat_cli.py`

### C. Crea `.env`

```bash
nano ~/GPT-E/.env
```

### D. Carica firmware ESP32 definitivo

### E. Crea servizi systemd

- `gpte-core.service`
- `gpte.service`

### F. Attiva servizi

```bash
sudo systemctl daemon-reload
sudo systemctl enable gpte-core.service
sudo systemctl enable gpte.service
sudo systemctl start gpte-core.service
sudo systemctl start gpte.service
```

### G. Verifica

```bash
systemctl status gpte-core.service
systemctl status gpte.service
ss -ltnp | grep 8765
ss -ltnp | grep 8766
ss -ltnp | grep 5001
```

### H. Usa la chat

```bash
source env/bin/activate
python chat_cli.py
```

## 14. Stato attuale del sistema

Con questa guida ottieni:

- core separato dal brain
- brain separato dalla chat
- UART usata da un solo processo
- ACK / heartbeat / state
- TTS servito da Flask
- ESP32 che riproduce audio via HTTP
- chat da terminale pulita
- supporto a `pulse_servo`
- supporto a `oscillate_servo`
- testa: canale 14 destra/sinistra, canale 15 su/giù
