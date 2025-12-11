#!/usr/bin/env python3
import json
import logging
import re
import socket
import threading
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Callable

import requests  # <-- added

from signalrcore.hub_connection_builder import HubConnectionBuilder
from signalrcore.messages.completion_message import CompletionMessage

# ---------------------------------------------------------------------------
# Constants & logging
# ---------------------------------------------------------------------------

CONFIG_FILE = "bridgeconfig.json"
DISCOVERY_PORT = 5390
DISCOVERY_PREFIX = "OPENHANDY_DISCOVERY"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

@dataclass
class BridgeConfig:
    voxta_base_url: str
    signalr_url: str
    action_context_key: str
    voxta_action: Dict[str, Any]

    @staticmethod
    def load(path: str = CONFIG_FILE) -> "BridgeConfig":
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)

        voxta_base_url = data["voxtaBaseUrl"].rstrip("/")
        signalr_url = data["signalRUrl"].rstrip("/")
        action_context_key = data.get("actionContextKey", "OpenHandyActions")
        voxta_action = data["voxtaAction"]

        logging.info(
            "Config loaded: voxtaBaseUrl=%s signalRUrl=%s actionName=%s contextKey=%s",
            voxta_base_url,
            signalr_url,
            voxta_action.get("name"),
            action_context_key,
        )

        return BridgeConfig(
            voxta_base_url=voxta_base_url,
            signalr_url=signalr_url,
            action_context_key=action_context_key,
            voxta_action=voxta_action,
        )

# ---------------------------------------------------------------------------
# UDP discovery listener
# ---------------------------------------------------------------------------

class DiscoveryListener(threading.Thread):
    """
    Listens for UDP broadcasts like:
      OPENHANDY_DISCOVERY ip=10.20.0.101 host=openhandy tcode=2000 dport=5390 ...
    Only the FIRST valid discovery is forwarded; repeats are ignored.
    """

    def __init__(
        self,
        listen_port: int = DISCOVERY_PORT,
        on_discover: Optional[Callable[[str, str], None]] = None,
    ):
        super().__init__(daemon=True)
        self.listen_port = listen_port
        self._stop_event = threading.Event()
        self.latest_ip: Optional[str] = None
        self._device_locked: bool = False
        self.on_discover = on_discover

    def run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("", self.listen_port))
        except OSError as e:
            logging.error("Failed to bind UDP discovery socket: %s", e)
            return

        logging.info(
            "Discovery listener started on UDP 0.0.0.0:%d (prefix=%s)",
            self.listen_port,
            DISCOVERY_PREFIX,
        )

        while not self._stop_event.is_set():
            try:
                sock.settimeout(1.0)
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break

            try:
                text = data.decode("utf-8", errors="replace").strip()
            except Exception:
                text = repr(data)

            if not text.startswith(DISCOVERY_PREFIX):
                continue

            # Only accept the first device we see
            if self._device_locked:
                continue

            m = re.search(r"\bip=([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)", text)
            ip = m.group(1) if m else addr[0]

            self.latest_ip = ip
            self._device_locked = True

            logging.info("Device discovery: ip=%s raw=%s", ip, text)

            if self.on_discover:
                try:
                    self.on_discover(ip, text)
                except Exception as ex:
                    logging.error("on_discover callback error: %s", ex)

        sock.close()

    def stop(self) -> None:
        self._stop_event.set()

# ---------------------------------------------------------------------------
# Voxta bridge
# ---------------------------------------------------------------------------

class VoxtaOpenHandyBridge:
    def __init__(self, cfg: BridgeConfig, discovery: DiscoveryListener):
        self.cfg = cfg
        self.discovery = discovery

        # Auth / user
        self._authenticated: bool = False
        self._user_id: Optional[str] = None
        self._user_name: Optional[str] = None

        # Chat / session
        self._session_id: Optional[str] = None
        self._chat_id: Optional[str] = None
        self._character_name: Optional[str] = None

        # Device
        self._device_ip: Optional[str] = None

        # Injection tracking
        self._injected_for_session: Optional[str] = None
        self._logged_no_device_for_session: Optional[str] = None

        # Runtime
        self._stop_event = threading.Event()
        self._hub = None
        self._hub_thread = threading.Thread(
            target=self._run_signalr_loop, name="signalr", daemon=True
        )

    # ---------------------------- lifecycle ----------------------------

    def start(self) -> None:
        logging.info(
            "Starting bridge: voxtaBaseUrl=%s signalRUrl=%s",
            self.cfg.voxta_base_url,
            self.cfg.signalr_url,
        )
        self._hub_thread.start()

    def stop(self) -> None:
        logging.info("Stopping bridge...")
        self._stop_event.set()
        if self._hub:
            try:
                self._hub.stop()
            except Exception:
                pass

    # Called by DiscoveryListener on first device broadcast
    def on_device_discover(self, ip: str, text: str) -> None:
        logging.info("Device discovery callback: %s", ip)
        self._device_ip = ip
        # reset per-session log gate once we have a device
        self._logged_no_device_for_session = None
        self._maybe_inject_actions()

    # ----------------------- SignalR connection ------------------------

    def _build_hub(self):
        hub = (
            HubConnectionBuilder()
            .with_url(self.cfg.signalr_url)
            .configure_logging(logging.WARNING)
            .with_automatic_reconnect(
                {
                    "type": "raw",
                    "keep_alive_interval": 15,
                    "reconnect_interval": 5,
                    "max_attempts": 9999,
                }
            )
            .build()
        )

        hub.on_open(self._on_hub_open)
        hub.on_close(self._on_hub_close)
        hub.on_error(self._on_hub_error)
        hub.on("ReceiveMessage", self._on_hub_message)

        return hub

    def _run_signalr_loop(self) -> None:
        while not self._stop_event.is_set():
            try:
                logging.info("Creating SignalR connection to %s", self.cfg.signalr_url)
                self._hub = self._build_hub()
                self._hub.start()
                while not self._stop_event.is_set():
                    time.sleep(1.0)
                break
            except Exception as ex:
                logging.error("SignalR connection failed or closed: %s", ex)
                if self._stop_event.is_set():
                    break
                logging.info("Retrying SignalR connection in 5 seconds...")
                time.sleep(5.0)

    # ----------------------------- hub events -----------------------------

    def _on_hub_open(self):
        logging.info("SignalR connection opened, sending authenticate...")
        self._authenticated = False
        self._user_id = None
        self._user_name = None
        self._send_authenticate()

    def _on_hub_close(self):
        logging.info("SignalR connection closed")
        self._authenticated = False
        self._session_id = None
        self._chat_id = None
        self._character_name = None
        self._injected_for_session = None
        self._logged_no_device_for_session = None

    def _on_hub_error(self, error):
        if isinstance(error, CompletionMessage):
            logging.error(
                "SignalR completion error: invocation_id=%s error=%s result=%s",
                getattr(error, "invocation_id", None),
                getattr(error, "error", None),
                getattr(error, "result", None),
            )
        else:
            logging.error("SignalR error: %r", error)

    def _on_hub_message(self, args: List[Any]):
        if not args:
            return

        payloads = args[0]
        if isinstance(payloads, dict):
            payloads = [payloads]

        if not isinstance(payloads, list):
            logging.debug("Ignoring hub message of type %r", type(payloads))
            return

        for p in payloads:
            if isinstance(p, dict):
                self._handle_voxta_payload(p)

    # ------------------------- Voxta protocol -------------------------

    def _send_messages(self, messages: List[Dict[str, Any]]) -> None:
        """
        EXACT Node-RED behavior:

        Node-RED: sendToSignalR(payload)
          -> payload = Array.isArray(payload) ? payload : [payload]
          -> signalr-out: hub.send("SendMessage", msg.payload)

        So args = [msg1, msg2, ...]

        Here, `messages` IS that array. We pass it straight through.
        """
        if not self._hub:
            logging.error("Cannot send: hub not initialized")
            return

        if not isinstance(messages, list):
            messages = [messages]

        try:
            # IMPORTANT: no extra [] wrapper
            self._hub.send("SendMessage", messages)
        except Exception as ex:
            logging.error("Failed to send to hub: %s", ex)

    def _send_authenticate(self) -> None:
        # Node-RED / NoxyHub authenticate payload (known good)
        auth_msg = {
            "$type": "authenticate",
            "client": "Voxta.OpenHandyBridge",
            "clientVersion": "1.0.0",
            "scope": ["role:app", "role:inspector"],
            "capabilities": {
                "audioInput": "WebSocketStream",
                "audioOutput": "Url",
            },
        }
        logging.info("Sending Voxta authenticate message...")
        self._send_messages([auth_msg])

    def _handle_voxta_payload(self, payload: Dict[str, Any]) -> None:
        msg_type = payload.get("$type")
        if not msg_type:
            return

        if msg_type == "welcome":
            self._on_welcome(payload)
        elif msg_type == "chatsSessionsUpdated":
            self._on_sessions_updated(payload)
        elif msg_type == "chatStarted":
            self._on_chat_started(payload)
        elif msg_type in ("chatClosed", "chatEnded"):
            self._on_chat_ended(payload)
        elif msg_type == "action":
            self._on_action(payload)
        elif msg_type == "error":
            logging.error("Voxta error: %s", payload)
        else:
            logging.debug("Voxta message: %s", msg_type)

    def _on_welcome(self, payload: Dict[str, Any]) -> None:
        self._authenticated = True
        user = payload.get("user") or {}
        self._user_id = user.get("id")
        self._user_name = user.get("name")
        logging.info(
            "Authenticated as %s (id=%s), server=%s api=%s",
            self._user_name,
            self._user_id,
            payload.get("voxtaServerVersion"),
            payload.get("apiVersion"),
        )

    def _on_sessions_updated(self, payload: Dict[str, Any]) -> None:
        sessions = payload.get("sessions") or []
        if not sessions:
            if self._session_id:
                logging.info("Active chat closed (sessions list empty)")
            self._session_id = None
            self._chat_id = None
            self._character_name = None
            self._injected_for_session = None
            self._logged_no_device_for_session = None
            return

        active = sessions[0]
        new_session = active.get("sessionId")
        new_chat = active.get("chatId")

        chars = active.get("characters") or []
        char_name = chars[0].get("name") if chars else None

        prev_session = self._session_id
        self._session_id = new_session
        self._chat_id = new_chat
        self._character_name = char_name

        logging.info(
            "Chat sessions updated: sessionId=%s chatId=%s character=%s (previous=%s)",
            self._session_id,
            self._chat_id,
            self._character_name,
            prev_session,
        )

        if prev_session != new_session:
            self._injected_for_session = None
            self._logged_no_device_for_session = None

        # Auto-subscribe like Node-RED
        if self._session_id:
            sub_msg = {
                "$type": "subscribeToChat",
                "sessionId": self._session_id,
            }
            self._send_messages([sub_msg])

        self._maybe_inject_actions()

    def _on_chat_started(self, payload: Dict[str, Any]) -> None:
        sid = payload.get("sessionId")

        # ***** IMPORTANT FIX: ignore duplicate chatStarted for same session *****
        if sid == self._session_id and self._session_id is not None:
            # Don’t reset flags, don’t log, don’t re-inject.
            logging.debug(
                "Duplicate chatStarted received for session %s; ignoring", sid
            )
            return

        chars = payload.get("characters") or []
        char_name = chars[0].get("name") if chars else None

        prev_session = self._session_id
        self._session_id = sid
        self._chat_id = payload.get("chatId")
        self._character_name = char_name

        logging.info(
            "Chat started: sessionId=%s character=%s (previous=%s)",
            self._session_id,
            self._character_name,
            prev_session,
        )

        # Reset per-session gates ONLY when session actually changes
        self._injected_for_session = None
        self._logged_no_device_for_session = None

        if self._session_id:
            sub_msg = {
                "$type": "subscribeToChat",
                "sessionId": self._session_id,
            }
            self._send_messages([sub_msg])

        self._maybe_inject_actions()

    def _on_chat_ended(self, payload: Dict[str, Any]) -> None:
        logging.info("Chat ended: sessionId=%s", self._session_id)
        self._session_id = None
        self._chat_id = None
        self._character_name = None
        self._injected_for_session = None
        self._logged_no_device_for_session = None

    # ----------------------- Action injection -----------------------

    def _build_action_definition(self) -> Dict[str, Any]:
        va = self.cfg.voxta_action

        action_def: Dict[str, Any] = {
            "name": va["name"],
            "description": va.get("description", f"Action: {va['name']}"),
            "timing": va.get("timing", "AfterAssistantMessage"),
            "layer": va.get("layer", "default"),
            "effect": {
                "secret": va.get("secret", ""),
                "note": va.get("note", ""),
                "setFlags": va.get("setFlags", []),
            },
        }

        if "arguments" in va:
            action_def["arguments"] = [
                {
                    "name": arg["name"],
                    "type": arg.get("type", "String"),
                    "required": arg.get("required", False),
                    "description": arg.get("description", ""),
                }
                for arg in va["arguments"]
            ]

        return action_def

    def _maybe_inject_actions(self) -> None:
        if not self._authenticated or not self._session_id:
            return

        if not self._device_ip:
            # Only log once per session, not flood
            if self._logged_no_device_for_session != self._session_id:
                logging.info(
                    "Device not discovered yet; postpone action injection for session %s",
                    self._session_id,
                )
                self._logged_no_device_for_session = self._session_id
            return

        if self._injected_for_session == self._session_id:
            return

        action_def = self._build_action_definition()

        update_ctx = {
            "$type": "updateContext",
            "sessionId": self._session_id,
            "contextKey": self.cfg.action_context_key,
            "actions": [action_def],
        }

        logging.info(
            "Injecting actions into Voxta context: [%s] (sessionId=%s, device=%s)",
            action_def["name"],
            self._session_id,
            self._device_ip,
        )

        # Send as an ARRAY OF MESSAGES, matching Node-RED semantics
        self._send_messages([update_ctx])
        self._injected_for_session = self._session_id

    # ---------------------- Device HTTP control -----------------------

    def _execute_device_action(self, args: Dict[str, Any]) -> None:
        """
        Translate action arguments into OpenHandy HTTP calls.

        Order (always, for every invocation):
          1. setpattern
          2. setspeed (if provided)
          3. start / stop

        Debounced with ~100 ms between calls to avoid flooding the device.
        """
        if not self._device_ip:
            logging.warning("No device discovered yet; cannot execute device action")
            return

        base_url = f"http://{self._device_ip}"

        # Extract arguments (all are strings from Voxta)
        motion_state = (args.get("motion_state") or "").strip().lower()
        stroke_type = (args.get("stroke_type") or "").strip().lower()
        speed_raw = (args.get("speed") or "").strip()

        # 1) Stroke pattern → /api/motion?action=setpattern&mode=<int>
        pattern_map = {
            "sine": 0,
            "bounce": 1,
            "double_bounce": 2,
        }
        mode = pattern_map.get(stroke_type)
        if mode is None:
            logging.warning(
                "Unknown stroke_type '%s', defaulting to 'sine' (mode=0)", stroke_type
            )
            mode = 0

        try:
            url = f"{base_url}/api/motion?action=setpattern&mode={mode}"
            logging.info("Device request: %s", url)
            requests.get(url, timeout=2.0)
        except Exception as ex:
            logging.error("Failed to call setpattern on device %s: %s", self._device_ip, ex)

        # Debounce between requests
        time.sleep(0.1)

        # 2) Speed → /api/motion?action=setspeed&sp=<int>
        if speed_raw:
            try:
                sp = int(speed_raw)
            except ValueError:
                logging.warning("Invalid speed value '%s', skipping setspeed", speed_raw)
            else:
                # Clamp to 10–100 as per your spec; device itself supports 0–100.
                if sp < 10:
                    sp = 10
                if sp > 100:
                    sp = 100

                try:
                    url = f"{base_url}/api/motion?action=setspeed&sp={sp}"
                    logging.info("Device request: %s", url)
                    requests.get(url, timeout=2.0)
                except Exception as ex:
                    logging.error("Failed to call setspeed on device %s: %s", self._device_ip, ex)

                time.sleep(0.1)

        # 3) Start / stop → /api/motion?action=start|stop
        if motion_state in ("start", "stop"):
            try:
                url = f"{base_url}/api/motion?action={motion_state}"
                logging.info("Device request: %s", url)
                requests.get(url, timeout=2.0)
            except Exception as ex:
                logging.error(
                    "Failed to call %s on device %s: %s",
                    motion_state,
                    self._device_ip,
                    ex,
                )
        else:
            logging.warning(
                "Unknown motion_state '%s', skipping start/stop", motion_state
            )

    # ---------------------- Action invocation -----------------------

    def _on_action(self, payload: Dict[str, Any]) -> None:
        action_name = payload.get("value")
        args_map: Dict[str, Any] = {}

        for arg in payload.get("arguments") or []:
            name = arg.get("name")
            val = arg.get("value")
            if name:
                args_map[name] = val

        logging.info("Action triggered: %s args=%s", action_name, args_map)

        # Only handle the configured action for this bridge
        if action_name == self.cfg.voxta_action.get("name"):
            self._execute_device_action(args_map)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    cfg = BridgeConfig.load()

    # Bridge placeholder so discovery callback can reference instance
    bridge_holder: List[VoxtaOpenHandyBridge] = []

    def on_discover(ip: str, text: str) -> None:
        bridge_holder[0].on_device_discover(ip, text)

    discovery = DiscoveryListener(listen_port=DISCOVERY_PORT, on_discover=on_discover)
    bridge = VoxtaOpenHandyBridge(cfg, discovery)
    bridge_holder.append(bridge)

    discovery.start()
    bridge.start()

    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        bridge.stop()
        discovery.stop()


if __name__ == "__main__":
    main()
