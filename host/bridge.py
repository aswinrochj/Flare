#!/usr/bin/env python3
"""Entry point named in the documentation: `python bridge.py --port /dev/ttyUSB0`.
Same as `python -m safenode_host`."""

import sys

from safenode_host.app import main

if __name__ == "__main__":
    sys.exit(main())
