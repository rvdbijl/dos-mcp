"""Wire constants. Numeric values are part of protocol version 2."""

from enum import IntEnum, IntFlag

MAGIC = b"DM"
VERSION = 2
DEFAULT_PORT = 21300
MAX_DATAGRAM = 1200
MAX_FRAGMENT_PAYLOAD = 1024
MAX_FRAGMENTS = 32
MAX_MESSAGE_SIZE = MAX_FRAGMENT_PAYLOAD * MAX_FRAGMENTS


class MessageKind(IntEnum):
    REQUEST = 1
    RESPONSE = 2
    ERROR = 3


class Opcode(IntEnum):
    HELLO = 1
    GET_STATUS = 2
    GET_CAPABILITIES = 3
    CAPTURE_TEXT_SCREEN = 4
    SEND_KEYS = 5
    PING = 6
    CANCEL = 7
    FILE_READ_BEGIN = 8
    FILE_READ_BLOCK = 9
    FILE_READ_END = 10
    FILE_WRITE_BEGIN = 11
    FILE_WRITE_BLOCK = 12
    FILE_WRITE_COMMIT = 13
    FILE_ABORT = 14
    GRAPHICS_BEGIN = 15
    GRAPHICS_BLOCK = 16
    GRAPHICS_END = 17
    GET_DIAGNOSTICS = 18


class ErrorCode(IntEnum):
    MALFORMED = 1
    UNSUPPORTED_VERSION = 2
    AUTHENTICATION_FAILED = 3
    REPLAYED_REQUEST = 4
    UNSUPPORTED_OPERATION = 5
    INVALID_ARGUMENT = 6
    DENIED = 7
    TARGET_BUSY = 8
    TIMEOUT = 9
    CANCELLED = 10
    INTEGRITY_FAILURE = 11
    INTERNAL_FAILURE = 12


class Capability(IntFlag):
    STATUS = 1 << 0
    TEXT_CAPTURE = 1 << 1
    KEYBOARD = 1 << 2
    FILESYSTEM_READ = 1 << 3
    FILESYSTEM_WRITE = 1 << 4
    EXECUTION = 1 << 5
    MEMORY_READ = 1 << 6
    MEMORY_WRITE = 1 << 7
    PORT_READ = 1 << 8
    PORT_WRITE = 1 << 9
    REBOOT = 1 << 10
    GRAPHICS_CAPTURE = 1 << 11


class Phase(IntEnum):
    STARTING = 1
    OBSERVE_READY = 2
    AGENT_SHELL_READY = 3
    CHILD_RUNNING = 4
    DOS_BUSY = 5
    AWAITING_APPROVAL = 6
    HOST_UNRESPONSIVE = 7


class Adapter(IntEnum):
    UNKNOWN = 0
    MDA = 1
    CGA = 2
    EGA = 3
    VGA = 4
    LINUX_PTY = 254


class GraphicsLayout(IntEnum):
    CGA_2BPP_INTERLEAVED = 1
    CGA_1BPP_INTERLEAVED = 2
    HERCULES_1BPP_INTERLEAVED = 3
    PLANAR_4BPP = 4
    PLANAR_1BPP = 5
    PACKED_8BPP = 6


class ResidentDiagnosticFlag(IntFlag):
    OWNS_INT08 = 1 << 0
    OWNS_INT1C = 1 << 1
    OWNS_INT28 = 1 << 2
    OWNS_INT2F = 1 << 3
    ENABLED = 1 << 4
    RECEIVE_READY = 1 << 5
    SESSION_ACTIVE = 1 << 6
    RESPONSE_PENDING = 1 << 7


class KeyCode(IntEnum):
    ENTER = 1
    ESC = 2
    TAB = 3
    BACKSPACE = 4
    UP = 5
    DOWN = 6
    RIGHT = 7
    LEFT = 8
    HOME = 9
    END = 10
    DELETE = 11
    PAGE_UP = 12
    PAGE_DOWN = 13
    F1 = 14
    F2 = 15
    F3 = 16
    F4 = 17
    F5 = 18
    F6 = 19
    F7 = 20
    F8 = 21
    F9 = 22
    F10 = 23
    F11 = 24
    F12 = 25
    CTRL_C = 26
    CTRL_D = 27


KEY_NAMES = {key.name: key for key in KeyCode}
