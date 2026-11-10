"""Save the device's SETUP_INFO response into the private setup file without losing other keys.

The setup file (`.build/device-setup.json`, mode 0600) also holds keys the firmware does not send,
such as the administrator password, so a new response is merged into it rather than replacing it.
"""
import json
import os
from pathlib import Path


def merge_setup_response(existing, response):
    """Return `existing` updated with `response`, the setup network key renamed for clarity.

    The firmware calls the setup network's key 'password'; the file calls it 'wifi_setup_password'
    so it is not mistaken for the administrator password.
    """
    merged = dict(existing)
    update = dict(response)
    if 'password' in update:
        update['wifi_setup_password'] = update.pop('password')
    merged.update(update)
    return merged


def save_setup_response(path, response):
    """Merge `response` into the setup file at `path`, created or kept at mode 0600."""
    path = Path(path)
    existing = json.loads(path.read_text()) if path.exists() else {}
    merged = merge_setup_response(existing, response)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, 'w') as output:
        json.dump(merged, output, indent=2)
        output.write('\n')
    return merged
