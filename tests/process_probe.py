"""Best-effort child names; Linux procfs may lose a task during open/read."""
from pathlib import Path


def process_name(pid):
    try:
        return Path(f'/proc/{pid}/comm').read_text().strip()
    except (FileNotFoundError, ProcessLookupError):
        return None
