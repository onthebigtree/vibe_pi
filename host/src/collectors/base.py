from abc import ABC, abstractmethod
from typing import Any


class BaseCollector(ABC):
    @property
    @abstractmethod
    def name(self) -> str: ...

    @property
    @abstractmethod
    def display_name(self) -> str: ...

    @abstractmethod
    async def collect(self) -> dict[str, Any] | None: ...
