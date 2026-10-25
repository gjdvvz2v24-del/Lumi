#!/usr/bin/env python3
"""Paid host experiment: WAV -> Polish ASR -> Sol -> existing Lumi TTS.

No firmware changes, microphone capture, playback or device flashing.
Secrets come from environment variables only. Python standard library only.
"""
import argparse
import json
import os
from pathlib import Path
import time
import urllib.error
import urllib.request
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]


def request(url, body, key, content_type="application/json", eleven=False):
    headers = {"Content-Type": content_type}
    headers["xi-api-key" if eleven else "Authorization"] = key if eleven else "Bearer " + key
    return urllib.request.urlopen(urllib.request.Request(url, data=body, headers=headers), timeout=60)


def transcribe(path, profile, key):
    # A fixed filename avoids interpolating caller-controlled multipart headers.
    with wave.open(str(path)) as wav:
        if wav.getcomptype() != "NONE" or wav.getnchannels() != 1 or wav.getsampwidth() != 2:
            raise ValueError("Input must be mono PCM16 WAV")
    if path.stat().st_size > 24_000_000:
        raise ValueError("Input WAV exceeds this test's 24 MB limit")
    boundary = "lumi-" + uuid.uuid4().hex
    parts = []
    for name, value in {"model": profile["transcription"]["model"], "language": "pl"}.items():
        parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode())
    parts.extend([f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="input.wav"\r\nContent-Type: audio/wav\r\n\r\n'.encode(), path.read_bytes(), f'\r\n--{boundary}--\r\n'.encode()])
    start = time.monotonic()
    with request("https://api.openai.com/v1/audio/transcriptions", b"".join(parts), key,
                 "multipart/form-data; boundary=" + boundary) as response:
        text = json.load(response)["text"].strip()
    if not text:
        raise RuntimeError("Empty transcription")
    return text, time.monotonic() - start


def sse_events(lines):
    data = []
    for line in lines:
        line = line.decode("utf-8").rstrip("\r\n")
        if not line:
            if data:
                payload = "\n".join(data)
                data = []
                if payload != "[DONE]":
                    yield json.loads(payload)
        elif line.startswith("data:"):
            data.append(line[5:].lstrip(" "))
    if data and "\n".join(data) != "[DONE]":
        yield json.loads("\n".join(data))


def extract_reply(response):
    if response.get("status") != "completed":
        raise RuntimeError("Response did not complete: " + str(response.get("incomplete_details") or response.get("error")))
    calls = [x for x in response.get("output", []) if x.get("type") == "function_call"]
    if calls:
        if len(calls) != 1 or calls[0]["name"] != "end_conversation":
            raise RuntimeError("Unsupported tool call")
        farewell = json.loads(calls[0]["arguments"]).get("farewell")
        if not isinstance(farewell, str) or not farewell.strip():
            raise RuntimeError("Invalid farewell")
        # Match firmware behavior: speak farewell exactly once, then stop.
        return farewell.strip(), True
    text = "".join(part.get("text", "") for item in response.get("output", [])
                   if item.get("type") == "message" for part in item.get("content", [])
                   if part.get("type") == "output_text").strip()
    if not text:
        raise RuntimeError("Empty assistant response")
    return text, False


def generate(text, profile, prompt, key, tier, previous=None):
    body = {"model": "gpt-6.1-sol", "service_tier": tier,
            "reasoning": {"effort": "low"}, "stream": True,
            "instructions": prompt, "input": text,
            "max_output_tokens": profile["max_output_tokens"], "tools": profile["tools"]}
    if previous:
        body["previous_response_id"] = previous
    start = time.monotonic()
    first = None
    completed = None
    with request("https://api.openai.com/v1/responses", json.dumps(body).encode(), key) as response:
        for event in sse_events(response):
            kind = event.get("type")
            if kind in ("response.output_text.delta", "response.function_call_arguments.delta") and first is None:
                first = time.monotonic() - start
            if kind == "response.completed":
                completed = event["response"]
            if kind in ("error", "response.failed", "response.incomplete"):
                raise RuntimeError("Generation failed: " + kind)
    if completed is None:
        raise RuntimeError("Stream closed without response.completed")
    if completed.get("service_tier") != tier:
        raise RuntimeError("Requested tier was not served: " + str(completed.get("service_tier")))
    reply, end = extract_reply(completed)
    return reply, {"model": completed["model"], "service_tier": completed["service_tier"],
                   "first_delta_s": first, "generation_s": time.monotonic() - start,
                   "usage": completed.get("usage"), "end_conversation": end}, completed["id"]


def synthesize(text, profile, key, output):
    start = time.monotonic()
    first = None
    pcm = bytearray()
    url = "https://api.elevenlabs.io/v1/text-to-speech/" + profile["tts"]["voice_id"] + "/stream?output_format=pcm_16000"
    body = json.dumps({"text": text, "model_id": profile["tts"]["model_id"], "language_code": "pl"}).encode()
    with request(url, body, key, eleven=True) as response:
        while True:
            chunk = response.read1(4096)
            if not chunk:
                break
            if first is None:
                first = time.monotonic() - start
            pcm.extend(chunk)
    if not pcm or len(pcm) % 2:
        raise RuntimeError("Invalid PCM output")
    with wave.open(str(output), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(pcm)
    return {"tts_first_audio_s": first, "tts_total_s": time.monotonic() - start,
            "audio_duration_s": len(pcm) / 32000, "voice_id": profile["tts"]["voice_id"],
            "tts_model": profile["tts"]["model_id"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--text", help="One text turn; no ASR")
    source.add_argument("--audio", type=Path, help="Mono PCM16 WAV; paid ASR")
    parser.add_argument("--tier", choices=["ultrafast", "default"], default="ultrafast")
    parser.add_argument("--tts", action="store_true", help="Paid ElevenLabs synthesis")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    key = os.environ.get("OPENAI_API_KEY")
    if not key:
        parser.error("Set OPENAI_API_KEY")
    if args.tts and not os.environ.get("ELEVENLABS_API_KEY"):
        parser.error("Set ELEVENLABS_API_KEY for --tts")
    profile = json.loads((ROOT / "agent-profile.json").read_text())
    prompt = (ROOT / profile["instructions_file"]).read_text()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {"hardware_tested": False, "transport": "HTTPS/SSE",
              "tts_waits_for_complete_text": True, "asr_s": 0}
    start = time.monotonic()
    text = args.text
    if args.audio:
        text, report["asr_s"] = transcribe(args.audio, profile, key)
    report["input"] = text
    reply, metrics, _ = generate(text, profile, prompt, key, args.tier)
    report.update(metrics)
    report["reply"] = reply
    if args.tts:
        report.update(synthesize(reply, profile, os.environ["ELEVENLABS_API_KEY"], args.output_dir / "reply.wav"))
        report["pipeline_first_audio_s"] = report["asr_s"] + report["generation_s"] + report["tts_first_audio_s"]
    report["total_s"] = time.monotonic() - start
    (args.output_dir / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    try:
        main()
    except urllib.error.HTTPError as exc:
        # Never dump request headers or credentials.
        detail = exc.read().decode("utf-8", errors="replace")
        raise SystemExit(f"HTTP {exc.code}: {detail}")
