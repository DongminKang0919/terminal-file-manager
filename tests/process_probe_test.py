"""Exercise both procfs disappearance errors, without masking access failures."""
from pathlib import Path
from unittest.mock import patch
from process_probe import process_name

for error in [FileNotFoundError, ProcessLookupError]:
    with patch.object(Path, 'read_text', side_effect=error):
        assert process_name(123) is None
with patch.object(Path, 'read_text', return_value='vim\n'):
    assert process_name(123)=='vim'
with patch.object(Path, 'read_text', side_effect=PermissionError):
    try:
        process_name(123)
    except PermissionError:
        pass
    else:
        raise AssertionError('Unexpected procfs errors must remain visible')
print('PASS: ENOENT/ESRCH child disappearance, names, and visible access failures')
