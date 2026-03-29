"""
conftest.py — pytest configuration for server/tests/

Adds server/ to sys.path so tests can import pipeline and main directly.
"""

import sys
from pathlib import Path

_SERVER_DIR = Path(__file__).parent.parent
if str(_SERVER_DIR) not in sys.path:
    sys.path.insert(0, str(_SERVER_DIR))
