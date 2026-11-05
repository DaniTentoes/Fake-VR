"""
Screen Reader bridge for Fake-VR.
Provides seamless text-to-speech and braille routing for NVDA, JAWS, and Windows accessibility.
"""

from __future__ import annotations
import sys
from typing import Optional

try:
    import accessible_output2.outputs.auto
    _HAS_ACCESSIBLE_OUTPUT = True
except ImportError:
    _HAS_ACCESSIBLE_OUTPUT = False


class ScreenReader:
    """Manages speech and braille announcements for NVDA and screen readers."""

    def __init__(self, verbose_console: bool = True):
        self.verbose_console = verbose_console
        self.speaker = None
        self.reader_name = "Console (Fallback)"

        if _HAS_ACCESSIBLE_OUTPUT:
            try:
                self.speaker = accessible_output2.outputs.auto.Auto()
                self.reader_name = getattr(self.speaker, "name", "Accessible Output")
            except Exception as e:
                print(f"Screen reader initialization fallback: {e}")
                self.speaker = None

    def speak(self, text: str, interrupt: bool = True) -> None:
        """
        Speaks text through NVDA / active screen reader.
        If interrupt is True, cuts off prior speech immediately (vital for menu navigation and coords).
        """
        if not text:
            return

        if self.verbose_console:
            print(f"[Speech]: {text}")

        if self.speaker:
            try:
                self.speaker.speak(text, interrupt=interrupt)
            except Exception as e:
                if self.verbose_console:
                    print(f"[ScreenReader Error]: {e}")

    def braille(self, text: str) -> None:
        """Sends a message to an attached refreshable braille display."""
        if not text:
            return

        if self.speaker and hasattr(self.speaker, "braille"):
            try:
                self.speaker.braille(text)
            except Exception:
                pass


# Global singleton
_default_reader: Optional[ScreenReader] = None


def get_screen_reader() -> ScreenReader:
    global _default_reader
    if _default_reader is None:
        _default_reader = ScreenReader()
    return _default_reader


def speak(text: str, interrupt: bool = True) -> None:
    get_screen_reader().speak(text, interrupt=interrupt)
