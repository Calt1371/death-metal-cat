#!/usr/bin/env python3
"""Quip Generator -- standalone agent from the Death Metal Cat GDD (Pillar 1 roster).

Calls the Claude API directly given a trigger type (+ optional context tag) and
returns strict JSON: {"line": "...", "sound_tag": "..."}. Not engine-integrated
yet -- this is the standalone proof-of-concept per the build outline (run this
first, no engine dependency).

Usage:
    python quip_generator.py --trigger kill --context "basic_enemy"
    python quip_generator.py --trigger damage
    python quip_generator.py --trigger environment --context "water_puddle"

    # Batch mode: generate several quips per trigger type at once for review/curation.
    python quip_generator.py --batch
    python quip_generator.py --batch --batch-count 5 --context "basic_enemy"
"""

import argparse
import json
import os
import re
import sys

from anthropic import Anthropic, AnthropicError

MODEL = "claude-sonnet-4-6"

TRIGGER_TYPES = ("kill", "damage", "environment")

# Locked verbatim per the GDD voice reference -- do not paraphrase or "improve" this.
SYSTEM_PROMPT = """You are the Quip Generator for Death Metal Cat, a 2D side-scroller. You write short in-character voice lines for the player character, Cayde.

VOICE (locked, do not deviate):
Cayde has a deep, rough voice. He speaks in short one-liners, not clever banter. Growls/snarls are represented in the text itself since there's no voice acting -- trailing sounds like "...grrrgh.", "Rrrgh -- nice try.", ALL CAPS for a snarled word, or a growled interjection like "Tch." / "Hnnh." before or after the line.
- Kill trigger: short, satisfied, a little menacing.
- Damage trigger: grunted, defiant, never whiny.
- Environment trigger: dry and deadpan. Cat-specific flavor (claws, disdain for water, etc.) is fine. "Meow" is allowed but only as a short, deep, aggressive bark-like sound -- never a soft/cute filler word, never used the way internet-cat-speak uses it. It should land more like a growled warning than a greeting, e.g. "MEOW." or "Meow -- back off." No cat puns like "purrfect." Same gravelly voice throughout -- not a cartoon cat.

OUTPUT FORMAT (strict -- this is the entire response):
Return ONLY a single JSON object. No preamble, no trailing commentary, no markdown code fences. Exactly this shape:
{"line": "<quip text, <=40 words>", "sound_tag": "<invented placeholder tag name>"}

sound_tag naming convention: invent a short descriptive placeholder name such as growl_short_01, snarl_low_02, grunt_hurt_01, dry_flat_01 -- these don't map to real audio files yet, they just need to be consistent enough that a human can later assign real sound bites to matching tag names. Pick words that match both the trigger type and the specific line's tone (e.g. a kill quip should read as a growl/snarl-flavored tag, a damage quip a grunt/hurt-flavored tag, an environment quip a dry/flat-flavored tag) -- and vary the trailing number so repeated calls don't all land on "_01"."""

TRIGGER_HINTS = {
    "kill": "The player character just landed a kill.",
    "damage": "The player character just took damage.",
    "environment": "The player character just triggered an environmental interaction (not combat).",
}


def build_user_prompt(trigger: str, context: str | None, avoid_lines: list[str] | None = None) -> str:
    lines = [f"Trigger type: {trigger}", TRIGGER_HINTS[trigger]]
    if context:
        lines.append(f"Context: {context}")
    if avoid_lines:
        already_used = "\n".join(f"- {l}" for l in avoid_lines)
        lines.append(
            "Already used earlier in this batch -- write something genuinely different, "
            "not just a reworded variant. Do not repeat or closely paraphrase any of these:\n"
            f"{already_used}"
        )
    lines.append("Generate one quip now, per the locked voice and the strict JSON output format.")
    return "\n".join(lines)


def _extract_json_object(raw: str) -> dict:
    """Strip accidental markdown fences before parsing -- the prompt forbids them,
    this is just a defensive tolerance for an occasional slip, not a format we rely on."""
    text = raw.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1] if "\n" in text else text[3:]
        if text.endswith("```"):
            text = text[: -3]
        text = text.strip()
        if text.lower().startswith("json"):
            text = text[4:].strip()
    return json.loads(text)


def generate_quip(
    client: Anthropic,
    trigger: str,
    context: str | None = None,
    avoid_lines: list[str] | None = None,
) -> dict:
    if trigger not in TRIGGER_TYPES:
        raise ValueError(f"trigger must be one of {TRIGGER_TYPES}, got {trigger!r}")

    response = client.messages.create(
        model=MODEL,
        max_tokens=200,
        system=SYSTEM_PROMPT,
        messages=[{"role": "user", "content": build_user_prompt(trigger, context, avoid_lines)}],
    )
    raw_text = next(block.text for block in response.content if block.type == "text")

    try:
        quip = _extract_json_object(raw_text)
    except json.JSONDecodeError as exc:
        raise ValueError(
            f"Model did not return valid JSON for trigger={trigger!r} context={context!r}. "
            f"Raw response:\n{raw_text}"
        ) from exc

    if "line" not in quip or "sound_tag" not in quip:
        raise ValueError(f"Model JSON missing required keys (line/sound_tag): {quip}")

    return quip


_SOUND_TAG_RE = re.compile(r"^(.*)_(\d+)$")


def dedup_sound_tags(quips: list[dict]) -> None:
    """Each call is independent (no shared context), so the model has no signal
    to avoid repeating its own default tag number across calls -- in practice
    most quips in a batch land on the same trailing number (e.g. every kill
    quip as snarl_low_03). Renumbers in place, mutating each quip's sound_tag,
    so every tag within this batch is unique: first occurrence of a tag is left
    untouched, later duplicates get bumped to the next free number for that
    same base name (preserving the model's descriptive wording, just fixing
    the number)."""
    seen: set[str] = set()
    for quip in quips:
        tag = quip.get("sound_tag")
        if not tag:
            continue  # error entries have no sound_tag

        match = _SOUND_TAG_RE.match(tag)
        if match:
            base, num_str = match.group(1), match.group(2)
            width = len(num_str)
            next_num = int(num_str)
        else:
            # No trailing _NN to increment -- treat the whole tag as the base.
            base, width, next_num = tag, 2, 1

        candidate = tag
        while candidate in seen:
            next_num += 1
            candidate = f"{base}_{next_num:0{width}d}"

        quip["sound_tag"] = candidate
        seen.add(candidate)


def run_single(client: Anthropic, trigger: str, context: str | None) -> None:
    quip = generate_quip(client, trigger, context)
    print(json.dumps(quip, indent=2))


# Extra attempts if the model returns a line that exactly matches one already
# accepted earlier in this batch -- on top of (not instead of) telling it in the
# prompt which lines are already used, since prompting alone doesn't guarantee it.
MAX_DUPLICATE_RETRIES = 2


def generate_unique_quip(client: Anthropic, trigger: str, context: str | None, used_lines: list[str]) -> dict:
    quip: dict = {}
    for attempt in range(MAX_DUPLICATE_RETRIES + 1):
        try:
            quip = generate_quip(client, trigger, context, avoid_lines=used_lines)
        except ValueError as exc:
            return {"error": str(exc)}
        if quip["line"] not in used_lines:
            return quip
        print(f"  [{trigger}] got an exact-duplicate line (attempt {attempt + 1}), retrying...", file=sys.stderr)
    return quip  # exhausted retries -- accept the duplicate rather than looping forever


def run_batch(client: Anthropic, count: int, context: str | None) -> None:
    results: dict[str, list[dict]] = {}
    for trigger in TRIGGER_TYPES:
        batch = []
        used_lines: list[str] = []
        for i in range(count):
            quip = generate_unique_quip(client, trigger, context, used_lines)
            if "line" in quip:
                used_lines.append(quip["line"])
            batch.append(quip)
            print(f"  [{trigger}] {i + 1}/{count} generated", file=sys.stderr)
        dedup_sound_tags(batch)
        results[trigger] = batch
    print(json.dumps(results, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Quip Generator -- Death Metal Cat's Cayde voice-line agent (standalone, calls Claude API directly)."
    )
    parser.add_argument("--trigger", choices=TRIGGER_TYPES, help="Trigger type for a single quip (required unless --batch).")
    parser.add_argument("--context", default=None, help="Optional context tag, e.g. enemy type name or interactable name.")
    parser.add_argument("--batch", action="store_true", help="Batch mode: generate --batch-count quips for each of the three trigger types.")
    parser.add_argument("--batch-count", type=int, default=10, help="Quips per trigger type in batch mode (default: 10).")
    args = parser.parse_args()

    if not args.batch and not args.trigger:
        parser.error("--trigger is required unless --batch is set")

    try:
        client = Anthropic()
    except AnthropicError as exc:
        print(f"Failed to initialize Anthropic client: {exc}", file=sys.stderr)
        print("Set the ANTHROPIC_API_KEY environment variable and try again.", file=sys.stderr)
        return 1

    try:
        if args.batch:
            run_batch(client, args.batch_count, args.context)
        else:
            run_single(client, args.trigger, args.context)
    except AnthropicError as exc:
        print(f"Claude API error: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
