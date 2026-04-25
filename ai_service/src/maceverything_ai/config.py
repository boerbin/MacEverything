from dataclasses import dataclass


@dataclass
class Config:
    ai_port: int = 19861
    maceverything_port: int = 19860
    ollama_url: str = "http://localhost:11434"
    ollama_model: str = "qwen2.5:3b"
    ollama_keep_alive: str = "5m"
    llm_timeout: float = 60.0
    backend: str = "ollama"
    claude_model: str = "claude-haiku-4-5-20251001"


config = Config()
