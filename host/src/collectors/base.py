from abc import ABC, abstractmethod
from typing import Any


class BaseCollector(ABC):
    """Base class for status data collectors."""

    @property
    @abstractmethod
    def name(self) -> str:
        ...

    @abstractmethod
    async def collect(self) -> dict[str, Any] | None:
        """Collect current status. Returns None if tool is not active."""
        ...
