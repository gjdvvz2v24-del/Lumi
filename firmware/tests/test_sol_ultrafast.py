"""Offline protocol regressions for the experimental host pipeline."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("sol", Path(__file__).resolve().parents[1] / "tools/check_sol_ultrafast.py")
sol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sol)


class ProtocolTests(unittest.TestCase):
    def test_sse_multiline_and_unterminated_final_event(self):
        events = list(sol.sse_events([b": keepalive\n", b"event: response.completed\n",
                                     b'data: {"type":\n', b'data: "response.completed"}\n',
                                     b"\n", b'data: {"type":"tail"}']))
        self.assertEqual(events, [{"type": "response.completed"}, {"type": "tail"}])

    def test_partial_response_never_spoken(self):
        with self.assertRaises(RuntimeError):
            sol.extract_reply({"status": "incomplete", "output": [{"type": "message", "content": [{"type": "output_text", "text": "partial"}]}]})

    def test_farewell_spoken_once_even_with_duplicate_message(self):
        self.assertEqual(sol.extract_reply({"status": "completed", "output": [
            {"type": "message", "content": [{"type": "output_text", "text": "Pa!"}]},
            {"type": "function_call", "name": "end_conversation", "arguments": '{"farewell":"Pa!"}'}]}), ("Pa!", True))

    def test_bad_tool_or_empty_text_rejected(self):
        for output in [[], [{"type": "function_call", "name": "other", "arguments": "{}"}],
                       [{"type": "function_call", "name": "end_conversation", "arguments": '{"farewell":42}'}]]:
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                sol.extract_reply({"status": "completed", "output": output})


if __name__ == "__main__":
    unittest.main()
