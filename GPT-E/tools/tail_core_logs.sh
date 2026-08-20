#!/usr/bin/env bash
set -euo pipefail

journalctl -u gpte-core.service -n 100 --no-pager
