"""Windows 命令行 UI。"""
import asyncio


class SimpleUI:
    async def ainput(self, prompt: str = "指令> ") -> str:
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, lambda: input(prompt))

    def print(self, msg: str) -> None:
        print(msg)

    def info(self, msg: str) -> None:
        self.print(f"[信息] {msg}")

    def warn(self, msg: str) -> None:
        self.print(f"[警告] {msg}")

    def error(self, msg: str) -> None:
        self.print(f"[错误] {msg}")


__all__ = ["SimpleUI"]
