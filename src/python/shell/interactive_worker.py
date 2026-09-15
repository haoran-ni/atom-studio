"""Persistent execution process. Protocol lives on a private copy of stdout."""
import builtins
import json
import os
import queue
import signal
import sys
import threading
import time
import traceback
import types
import ase
import numpy as np
from ase import Atoms
from _atom_studio_bridge import pack, unpack

_wire = os.fdopen(os.dup(sys.stdout.fileno()), "w", encoding="utf-8", buffering=1)
_input = sys.stdin
_wire_lock = threading.Lock()
_commands = queue.Queue()
_responses = {}
_pending = set()
_cancel = threading.Event()
_run = 0
_request = 0
_documents = {}
_originals = {}
_published = {}
_last_update = {}
_interval = 0.1
_sync_ok = True
_executing = False
_namespace = {"__name__": "__main__", "np": np, "ase": ase, "Atoms": Atoms}


def _interrupt(signum, frame):
    if _executing:
        raise KeyboardInterrupt("Stopped by user")


signal.signal(signal.SIGINT, _interrupt)


def _send(kind, **values):
    with _wire_lock:
        _wire.write("@ATOM_SHELL@" + json.dumps(dict(type=kind, run=_run, **values),
                                               allow_nan=False, separators=(",", ":")) + "\n")
        _wire.flush()


class Output:
    encoding = "utf-8"
    def __init__(self, channel):
        self.channel = channel
        self.window = time.monotonic()
        self.remaining = 100000
    def write(self, text):
        now = time.monotonic()
        if now - self.window >= 1:
            self.window, self.remaining = now, 100000
        length = len(text)
        if self.remaining <= 0:
            return length
        text = text[:self.remaining]
        self.remaining -= len(text)
        # Bound individual messages; the UI also bounds retained output.
        for start in range(0, len(text), 8192):
            _send("output", channel=self.channel, text=text[start:start + 8192])
        if self.remaining == 0:
            _send("output", channel=self.channel, text="\n[Output rate limit reached; additional output is omitted.]\n")
        return length
    def flush(self):
        pass
    def isatty(self):
        return False


sys.stdout = Output("stdout")
sys.stderr = Output("stderr")


def _read_commands():
    try:
        for line in _input:
            message = json.loads(line)
            kind = message.get("type")
            if kind == "ack":
                _pending.discard(message["id"])
            elif kind == "stop":
                _cancel.set()
            elif kind == "response":
                response = _responses.get(message["request"])
                if response is not None:
                    response.put(message)
            else:
                _commands.put(message)
    finally:
        # Parent went away: do not leave a simulation running without its UI.
        if os.name == "posix" and os.getpgrp() == os.getpid():
            import signal
            os.killpg(os.getpgrp(), signal.SIGKILL)
        os._exit(0)


threading.Thread(target=_read_commands, daemon=True).start()


def _check_cancel():
    if _cancel.is_set():
        raise KeyboardInterrupt("Stopped by user")


def _id_for(atoms):
    matches = [sid for sid in _documents if _namespace.get(f"STRUCT_{sid}") is atoms]
    if len(matches) != 1:
        raise ValueError("Use the Atoms object named STRUCT_N; register new objects with studio.add(atoms)")
    return matches[0]


def update(atoms, *, _final=False):
    """Publish geometry without calculating forces or energy; suitable for ASE attach()."""
    _check_cancel()
    sid = _id_for(atoms)
    now = time.monotonic()
    if not _final and (sid in _pending or now - _last_update.get(sid, 0) < _interval):
        return
    snapshot = pack(atoms)
    encoded = json.dumps(snapshot, sort_keys=True, allow_nan=False)
    if encoded == _published.get(sid):
        return
    _pending.add(sid)
    _last_update[sid] = now
    _send("frame", id=sid, snapshot=snapshot, final=_final)
    _published[sid] = encoded


def _ask(kind, **values):
    global _request
    _request += 1
    request = _request
    response = queue.Queue()
    _responses[request] = response
    _send(kind, request=request, **values)
    try:
        while True:
            _check_cancel()
            try:
                result = response.get(timeout=0.1)
                if result.get("error"):
                    raise RuntimeError(result["error"])
                return result
            except queue.Empty:
                pass
    finally:
        _responses.pop(request, None)


def add(atoms):
    """Add a new document, bind its STRUCT_N name, and return that Atoms object."""
    if any(_namespace.get(f"STRUCT_{sid}") is atoms for sid in _documents):
        atoms = atoms.copy()
    snapshot = pack(atoms)
    response = _ask("add", snapshot=snapshot)
    sid = response["id"]
    _documents[sid] = atoms
    _namespace[f"STRUCT_{sid}"] = atoms
    _originals[sid] = unpack(snapshot)
    _published[sid] = json.dumps(snapshot, sort_keys=True, allow_nan=False)
    print(f"Added STRUCT_{sid}: {len(atoms)} atoms")
    return atoms


def show(atoms):
    """Select a registered structure without changing the camera scale."""
    _ask("show", id=_id_for(atoms))


def original(sid):
    """Return an independent copy of the original imported geometry."""
    return _originals[int(sid)].copy()


studio = types.ModuleType("studio")
studio.update, studio.add, studio.show, studio.original = update, add, show, original
sys.modules["studio"] = studio
_namespace["studio"] = studio


def _sync(message):
    known = set(message["ids"])
    for sid in list(_documents):
        if sid not in known:
            _namespace.pop(f"STRUCT_{sid}", None)
            _documents.pop(sid, None)
            _originals.pop(sid, None)
            _published.pop(sid, None)
    for doc in message["documents"]:
        sid = doc["id"]
        incoming = unpack(doc["current"])
        existing = _namespace.get(f"STRUCT_{sid}")
        if isinstance(existing, Atoms):
            calculator = existing.calc
            existing.__dict__.update(incoming.__dict__)
            existing.calc = calculator
            incoming = existing
        _documents[sid] = incoming
        _namespace[f"STRUCT_{sid}"] = incoming
        _originals[sid] = unpack(doc["original"])
        _published[sid] = json.dumps(pack(incoming), sort_keys=True, allow_nan=False)


def _unsupported_input(*args, **kwargs):
    raise RuntimeError("input() is unavailable. Set values in the editor before running.")


def _trace(frame, event, arg):
    if frame.f_code.co_filename == "<ATOM-STUDIO>":
        _check_cancel()
        return _trace
    return None


_send("ready", python=sys.version.split()[0], ase=ase.__version__)
while True:
    command = _commands.get()
    if command["type"] == "sync":
        try:
            _sync(command)
            _sync_ok = True
        except BaseException:
            _sync_ok = False
            _send("sync_error", text=traceback.format_exc())
    elif command["type"] == "run":
        _run = command["run"]
        _cancel.clear()
        _pending.clear()
        _last_update.clear()
        _interval = 1.0 / max(1, min(30, command.get("fps", 10)))
        started = time.monotonic()
        _executing = True
        success = False
        old_input = builtins.input
        try:
            if not _sync_ok:
                raise RuntimeError("Structure synchronization failed. Fix the reported metadata error and restart Python.")
            os.chdir(command["directory"])
            builtins.input = _unsupported_input
            sys.settrace(_trace)
            exec(compile(command["code"], "<ATOM-STUDIO>", "exec"), _namespace, _namespace)
            sys.settrace(None)
            # Validate every registered object before publishing final states.
            for sid in list(_documents):
                atoms = _namespace.get(f"STRUCT_{sid}")
                if not isinstance(atoms, Atoms):
                    raise TypeError(f"STRUCT_{sid} must remain an ase.Atoms object")
                pack(atoms)
            for sid in list(_documents):
                update(_namespace[f"STRUCT_{sid}"], _final=True)
            success = True
        except KeyboardInterrupt:
            _send("output", channel="stderr", text="Stopped. Last published geometry was retained.\n")
        except BaseException:
            _send("output", channel="stderr", text=traceback.format_exc())
        finally:
            sys.settrace(None)
            _executing = False
            signal.signal(signal.SIGINT, _interrupt)
            builtins.input = old_input
            sys.stdout, sys.stderr = Output("stdout"), Output("stderr")
            _send("done", success=success, elapsed=time.monotonic() - started)
