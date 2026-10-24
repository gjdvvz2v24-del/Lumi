#!/usr/bin/env python3
"""The OpenAI fork tests its live network/mode gate with the event tests."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("test_openai.py")),run_name="__main__")
