"""
Accessible hierarchical menu system designed for NVDA and screen readers.
Provides intuitive Up/Down arrow navigation, Enter to activate, and Escape to back out.
"""

from __future__ import annotations
from typing import Callable, List, Optional, Any
from client.screen_reader import get_screen_reader


class MenuItem:
    """Represents a selectable menu entry."""
    def __init__(
        self,
        title: str,
        action: Optional[Callable[[], Any]] = None,
        sub_menu: Optional[Menu] = None,
        description: str = ""
    ):
        self.title = title
        self.action = action
        self.sub_menu = sub_menu
        self.description = description


class Menu:
    """Represents a navigable list of options spoken via NVDA."""
    def __init__(self, title: str, items: Optional[List[MenuItem]] = None, parent: Optional[Menu] = None):
        self.title = title
        self.items: List[MenuItem] = items or []
        self.parent = parent
        self.selected_index = 0

    def add_item(self, item: MenuItem) -> None:
        if item.sub_menu:
            item.sub_menu.parent = self
        self.items.append(item)

    def current_item(self) -> Optional[MenuItem]:
        if 0 <= self.selected_index < len(self.items):
            return self.items[self.selected_index]
        return None

    def navigate(self, delta: int) -> MenuItem:
        """Moves selection up (-1) or down (+1) and announces new item."""
        if not self.items:
            return None
        self.selected_index = (self.selected_index + delta) % len(self.items)
        item = self.current_item()
        self.announce_current()
        return item

    def announce_current(self) -> None:
        """Speaks the active item through NVDA with its position index."""
        item = self.current_item()
        if not item:
            get_screen_reader().speak("Menu is empty.")
            return

        total = len(self.items)
        idx = self.selected_index + 1
        msg = f"{item.title}, {idx} of {total}"
        if item.description:
            msg += f". {item.description}"
        get_screen_reader().speak(msg, interrupt=True)

    def announce_menu_open(self) -> None:
        """Announces menu title and first item upon opening."""
        total = len(self.items)
        msg = f"{self.title} menu, {total} items. "
        item = self.current_item()
        if item:
            msg += f"Selected: {item.title}, 1 of {total}"
        get_screen_reader().speak(msg, interrupt=True)


class MenuManager:
    """Controls the active menu stack and routes keyboard input."""
    def __init__(self):
        self.root_menu: Optional[Menu] = None
        self.active_menu: Optional[Menu] = None
        self.is_active = False

    def open_menu(self, menu: Menu) -> None:
        self.root_menu = menu
        self.active_menu = menu
        self.is_active = True
        self.active_menu.announce_menu_open()

    def close(self) -> None:
        self.is_active = False
        self.active_menu = None
        get_screen_reader().speak("Exited menu. Movement controls active.", interrupt=True)

    def handle_up(self) -> None:
        if self.is_active and self.active_menu:
            self.active_menu.navigate(-1)

    def handle_down(self) -> None:
        if self.is_active and self.active_menu:
            self.active_menu.navigate(1)

    def handle_select(self) -> None:
        """Activates selected item or drills into sub-menu."""
        if not self.is_active or not self.active_menu:
            return

        item = self.active_menu.current_item()
        if not item:
            return

        if item.sub_menu:
            item.sub_menu.parent = self.active_menu
            self.active_menu = item.sub_menu
            self.active_menu.selected_index = 0
            self.active_menu.announce_menu_open()
        elif item.action:
            item.action()

    def handle_back(self) -> None:
        """Steps back to parent menu, or closes if at root."""
        if not self.is_active or not self.active_menu:
            return

        if self.active_menu.parent:
            self.active_menu = self.active_menu.parent
            self.active_menu.announce_current()
        else:
            self.close()
