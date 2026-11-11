
import ctypes
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIBRARY = Path(os.environ.get(
    "CIPHERAPI_LIBRARY", ROOT / "build" / "libcipherapi.so"
))

_lib = ctypes.CDLL(str(LIBRARY))
_lib.capi_last_error.restype = ctypes.c_char_p

_lib.capi_client_create.argtypes = [
    ctypes.c_char_p, ctypes.c_uint16, ctypes.c_char_p
]
_lib.capi_client_create.restype = ctypes.c_void_p
_lib.capi_client_connect.argtypes = [ctypes.c_void_p]
_lib.capi_client_connect.restype = ctypes.c_int
_lib.capi_client_send_text.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int
]
_lib.capi_client_send_text.restype = ctypes.c_int

_lib.capi_client_send_image.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int
]
_lib.capi_client_send_image.restype = ctypes.c_int
_lib.capi_client_send_video.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int
]
_lib.capi_client_send_video.restype = ctypes.c_int
_lib.capi_client_receive_event.argtypes = [
    ctypes.c_void_p, ctypes.POINTER(ctypes.c_int),
    ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int
]
_lib.capi_client_receive_event.restype = ctypes.c_int

_lib.capi_client_receive.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int
]
_lib.capi_client_receive.restype = ctypes.c_int
_lib.capi_client_close.argtypes = [ctypes.c_void_p]
_lib.capi_client_destroy.argtypes = [ctypes.c_void_p]


def _error():
    value = _lib.capi_last_error()
    return value.decode(errors="replace") if value else "Unknown error"


class _SendMode:
    def __init__(self, client, fast):
        self._client = client
        self._fast = fast

    def __call__(self, message):
        return self._client._send(message, fast=self._fast)


class _SendDispatcher:
    def __init__(self, client):
        self._client = client
        self.ft = _SendMode(client, True)
        self.ff = _SendMode(client, False)

    def __call__(self, message, fast=False):
        return self._client._send(message, fast=fast)


class Client:
    def __init__(self, host="127.0.0.1", port=8080, library_dir=None):
        self._handle = None
        self._closed = False
        self.send = _SendDispatcher(self)
        directory = Path(library_dir or ROOT / "libraries")
        self._handle = _lib.capi_client_create(
            host.encode(), port, str(directory).encode()
        )
        if not self._handle:
            raise RuntimeError(_error())

    def connect(self):
        if self._closed:
            raise RuntimeError("Client is closed")
        if _lib.capi_client_connect(self._handle) != 0:
            raise RuntimeError(_error())
        return self

    def _send(self, message, fast=False):
        if self._closed:
            raise RuntimeError("Client is closed")
        if not isinstance(message, str):
            raise TypeError("message must be a string")
        if "\x00" in message:
            raise ValueError("NUL characters are not supported")
        if _lib.capi_client_send_text(
            self._handle, message.encode("utf-8"), int(bool(fast))
        ) != 0:
            raise RuntimeError(_error())

    def send_image(self, path, raw=False):
        if self._closed:
            raise RuntimeError("Client is closed")
        result = _lib.capi_client_send_image(
            self._handle, os.fsencode(path), int(bool(raw))
        )
        if result != 0:
            raise RuntimeError(_error())

    def send_video(self, path, checksum=True):
        if self._closed:
            raise RuntimeError("Client is closed")
        result = _lib.capi_client_send_video(
            self._handle, os.fsencode(path), int(bool(checksum))
        )
        if result != 0:
            raise RuntimeError(_error())

    def receive_event(self, timeout=1.0):
        if self._closed:
            raise RuntimeError("Client is closed")

        timeout_ms = -1 if timeout is None else max(0, int(timeout * 1000))
        buffer = ctypes.create_string_buffer(1024 * 1024 + 1)
        event_type = ctypes.c_int()

        result = _lib.capi_client_receive_event(
            self._handle,
            ctypes.byref(event_type),
            buffer,
            ctypes.sizeof(buffer),
            timeout_ms
        )

        if result == -2:
            return None
        if result < 0:
            raise RuntimeError(_error())

        value = buffer.raw[:result].decode("utf-8")
        names = {1: "text", 2: "image", 3: "video"}
        return names.get(event_type.value, "unknown"), value

    def receive(self, timeout=1.0):
        if self._closed:
            raise RuntimeError("Client is closed")
        timeout_ms = -1 if timeout is None else max(0, int(timeout * 1000))
        buffer = ctypes.create_string_buffer(1024 * 1024 + 1)
        result = _lib.capi_client_receive(
            self._handle, buffer, ctypes.sizeof(buffer), timeout_ms
        )
        if result == -2:
            return None
        if result < 0:
            raise RuntimeError(_error())
        return buffer.raw[:result].decode("utf-8")

    def close(self):
        if not self._closed:
            _lib.capi_client_close(self._handle)
            _lib.capi_client_destroy(self._handle)
            self._handle = None
            self._closed = True

    def __enter__(self):
        return self.connect()

    def __exit__(self, *args):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
