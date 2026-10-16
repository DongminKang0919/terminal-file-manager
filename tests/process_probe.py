"""Linux process identity/liveness probes; disappearance during read is normal."""
from pathlib import Path
import json
import time


def process_name(pid):
    try:
        return Path(f'/proc/{pid}/comm').read_text().strip()
    except (FileNotFoundError, ProcessLookupError):
        return None


def process_state(pid):
    """Identity and state; disappearance during read is normal, other errors propagate."""
    try:
        raw = Path(f'/proc/{pid}/stat').read_text()
    except (FileNotFoundError, ProcessLookupError):
        return None
    fields = raw[raw.rindex(')') + 2:].split()
    return dict(pid=int(pid), state=fields[0], ppid=int(fields[1]),
                pgid=int(fields[2]), start=int(fields[19]), exit=int(fields[49]))


def wait_stopped(identity, seconds=2):
    """Require execution to stop, not adoption/reaping by the host's PID 1.

    A zombie cannot execute or hold FDs. It is reported explicitly, never described
    as reaped. A still-executing (including D/T) process must fail the deadline.
    Identity includes starttime so PID reuse cannot make a different task pass/fail.
    """
    deadline = time.monotonic() + seconds
    history = []
    while True:
        state = process_state(identity['pid'])
        if not history or state != history[-1]:
            history.append(state)
        if state is None or state['start'] != identity['start'] or state['state'] == 'Z':
            print('PROCESS STOPPED ' + json.dumps(dict(identity=identity, observed=state,
                  reaped=state is None, history=history)), flush=True)
            return state
        if time.monotonic() >= deadline:
            raise AssertionError(('process still executing after cleanup deadline', identity, history))
        # State polling, bounded by a deadline; no assumption that this interval
        # is sufficient for cleanup. Permission and malformed-proc errors surface.
        time.sleep(.005)
