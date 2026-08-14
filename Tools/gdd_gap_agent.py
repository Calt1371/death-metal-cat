#!/usr/bin/env python3
"""
gdd_gap_agent.py

GDD Gap Agent -- Assignment 5. A goal-oriented agent that reads the GDD, scans the real
codebase, detects which GDD-described features don't exist yet, prioritizes the gaps with
written reasoning, and (as a SEPARATE, hand-written deliverable file -- see
Docs/Assignment5_README.md) generates real code for the #1-priority gap. This script implements
Steps 1-4; Step 5 (code generation) is deliberately not this script's job -- see the README for
why writing that file is a one-off act of engineering judgment, not something worth automating
into a fifth pipeline stage.

REUSE, NOT REIMPLEMENTATION: Step 1 reuses quip_generator.py's existing chunk_gdd_by_heading()
to walk the docx by heading (paragraphs AND tables, in true document order) -- there is exactly
ONE GDD-chunking implementation in this project, same "single shared math core" discipline
foundation_extractor.py/reachability_verifier.py/stress_test_room.py already established for
compute_gaps()/compute_derived_movement_constants(). (Note: the assignment brief that commissioned
this script named foundation_extractor.py as chunk_gdd_by_heading()'s home; a directory search
found it actually lives in quip_generator.py, added there for the Assignment 4 RAG pipeline. This
script imports it from its real location rather than either re-implementing it or leaving a
broken import.)

STEP 1 PARSING STRATEGY -- deterministic where the document structure supports it, honestly
NOT where it doesn't: two real sections are genuine pipe-delimited tables with a Status column
(2.1 Player Verbs, 4.2 Agent Roster) -- parsed by splitting on " | " and reading columns by
header name, never by position, so a reordered column survives. Several headings (2.2-2.5, 2.7,
4.1) are single-system prose blocks with one clean "Status: ..." sentence -- parsed by regex.
Section 5 (Explicit Out-of-Scope) and 4.3's "Human gates" list are both, in the real docx,
one-bullet-per-paragraph -- chunk_gdd_by_heading() preserves that as one line per paragraph, so
they're parsed by splitting the chunk text on newlines. The one genuinely messy case is the
"Status:" chunk nested under 2.6, which crams FIVE different systems' statuses into flowing
prose with no per-line delimiter -- handled by marker-anchored segmentation (search for each
system's own proper-noun name, in the order it actually appears, and slice the text between
consecutive matches). This is real code that re-slices live chunk text (not a hardcoded output),
but the marker names themselves are known, hardcoded knowledge of this one paragraph's shape --
same spirit as foundation_extractor.py's own gdd_documented_sequence constant, which is compared
against live-parsed data rather than trusted blindly. Sections 1, 3, and 4.4 are reviewed but
deliberately not parsed into feature records: 1 and 4.4 are narrative/cost commentary with no
discrete feature-with-status shape, and 3's agent table is an intentionally-lower-detail
duplicate of 4.2's (the GDD's own text says build status lives in Section 4, not Section 3).

STEP 3 GAP-DETECTION STRATEGY: deterministic keyword matching, no LLM. Extract significant words
from each open (non-resolved, non-out-of-scope) feature's name, normalize away separators, and
check whether they appear (as substrings, in either direction) among a codebase inventory built
from Source/PythonTest/*.h/*.cpp class/function/property names and Tools/*.py file/function/class
names. A feature counts as "found" if enough of its distinctive keywords turn up together against
real inventory tokens -- see find_gap_evidence()'s docstring for the exact rule and why a single
generic word isn't enough evidence on its own.

STEP 4 PRIORITIZATION: computes three real, inspectable sub-scores per gap (spec clarity via
schema-keyword density in the GDD's own text, an unmet-dependency count via cross-referencing
each gap's description against every OTHER open gap's name, and a reuse-signal count against a
fixed vocabulary drawn from this project's most-reused measurement module). Writing the actual
prose justification per gap is a real language-judgment task -- this project's own established
pattern (see balance_qa_reviewer.py, quip_generator.py's critic) is to let an LLM write that prose
from numbers that are already final, never to let it re-decide the ranking. This script attempts
exactly that (one Claude API call, given the final scores) and gracefully falls back to a
deterministic template if no ANTHROPIC_API_KEY is available -- disclosed plainly in the output
JSON's "reasoning_source" field per gap, never silently swapped.

Usage:
    python gdd_gap_agent.py
    python gdd_gap_agent.py --gdd-path Docs/Death_Metal_Cat_GDD_v4.docx --use-llm
"""

import argparse
import json
import os
import re
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
REPO_ROOT = os.path.join(TOOLS_DIR, "..")
DOCS_DIR = os.path.join(REPO_ROOT, "Docs")
SOURCE_DIR = os.path.join(REPO_ROOT, "Source", "PythonTest")
GDD_PATH = os.path.join(DOCS_DIR, "Death_Metal_Cat_GDD_v4.docx")

from quip_generator import chunk_gdd_by_heading  # noqa: E402 -- reused, not reimplemented (see module docstring)

# ================================================================================================
# STEP 1 -- GDD feature extraction
# ================================================================================================

# A feature is "resolved" (excluded from gap candidacy) if its stated_status starts with one of
# these words. Matched case-insensitively against the START of the status string, not a substring
# search, so e.g. "Not started (status unconfirmed...)" is never mistaken for "started" == done.
RESOLVED_STATUS_PREFIXES = ("done", "proven", "committed", "verified")

# A feature is out-of-scope (also excluded from gap candidacy, but for a different reason than
# "resolved" -- it's a deliberate non-goal, not finished work) if any of these substrings appear
# anywhere in its stated_status. Covers Section 5's own items, the 4.3 human gates, and the
# explicit "no orchestration agent" architecture decision.
OUT_OF_SCOPE_MARKERS = ("out-of-scope", "out of scope", "human gate", "non-goal")


def classify_status(stated_status: str) -> str:
    """Returns 'resolved', 'out_of_scope', or 'open'. See the module docstring's Step 3 section
    for why 'open' is the only bucket that ever becomes a gap candidate."""
    s = stated_status.strip().lower()
    if any(marker in s for marker in OUT_OF_SCOPE_MARKERS):
        return "out_of_scope"
    if s.startswith(RESOLVED_STATUS_PREFIXES):
        return "resolved"
    return "open"


def _clean(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def parse_table_chunk(chunk: dict) -> list[dict]:
    """Parses a chunk whose text is a pipe-delimited table (header row + data rows, exactly what
    chunk_gdd_by_heading() produces for a docx table: each row is ' | '.join(cell texts)).
    Reads columns BY HEADER NAME so a reordered/renamed column doesn't silently misparse -- exactly
    the same defensive posture foundation_extractor.py's read_gdd() already uses for the agent-
    roster table. Returns [] if this chunk's first line isn't a real table header (no 'Status'
    column), so callers can try this first and fall through to other parsers harmlessly."""
    lines = [ln for ln in chunk["text"].split("\n") if " | " in ln]
    if not lines:
        return []
    header = [c.strip() for c in lines[0].split(" | ")]
    if "Status" not in header:
        return []
    name_col = header[0]  # first column is always the feature/verb/agent name in both real tables here
    features = []
    for row_line in lines[1:]:
        cells = [c.strip() for c in row_line.split(" | ")]
        if len(cells) != len(header):
            continue  # malformed row (e.g. a stray non-table line that happened to contain ' | ') -- skip rather than misalign
        row = dict(zip(header, cells))
        name = row.get(name_col, "").strip()
        status = row.get("Status", "").strip()
        if not name or not status:
            continue
        other_cols = [f"{col}: {row[col]}" for col in header if col not in (name_col, "Status") and row[col]]
        features.append({
            "feature": name,
            "section": chunk["heading"],
            "stated_status": status,
            "description": _clean(". ".join(other_cols)),
        })
    return features


# Lines matching this are a stretch-goal/out-of-scope aside embedded in an otherwise Done table
# section (currently only 2.1 Player Verbs has one) -- split on top-level commas (not commas
# inside parentheses, since e.g. "Double Jump (planned as a future unlockable skill)" has none but
# a future entry might) so each named item becomes its own out-of-scope feature record.
_STRETCH_GOAL_RE = re.compile(r"^Stretch goal, not this deliverable:\s*(.*)$")


def _split_top_level_commas(text: str) -> list[str]:
    parts, depth, current = [], 0, []
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    if current:
        parts.append("".join(current).strip())
    return [p for p in parts if p]


def parse_stretch_goal_asides(chunk: dict) -> list[dict]:
    features = []
    for line in chunk["text"].split("\n"):
        m = _STRETCH_GOAL_RE.match(line.strip())
        if not m:
            continue
        for item in _split_top_level_commas(m.group(1).rstrip(".")):
            short_name = re.split(r"[(.]", item)[0].strip() or item
            features.append({
                "feature": short_name,
                "section": chunk["heading"],
                "stated_status": "Explicit out-of-scope",
                "description": f"Stretch goal, not this deliverable: {item}.",
            })
    return features


_STATUS_LINE_RE = re.compile(r"^Status:\s*(.*)$")


def parse_single_status_chunk(chunk: dict) -> list[dict]:
    """Headings that describe exactly ONE system/feature and end in a single 'Status: ...' line
    (2.2-2.5, 2.7, 4.1 all follow this shape). Returns [] if no such line is found, so callers can
    try this unconditionally."""
    lines = chunk["text"].split("\n")
    status_idx = next((i for i, ln in enumerate(lines) if _STATUS_LINE_RE.match(ln.strip())), None)
    if status_idx is None:
        return []
    status_text = _STATUS_LINE_RE.match(lines[status_idx].strip()).group(1)
    description = _clean(" ".join(lines[:status_idx]))
    feature_name = re.sub(r"^[\d.]+\s*", "", chunk["heading"]).strip()
    return [{
        "feature": feature_name,
        "section": chunk["heading"],
        "stated_status": status_text,
        "description": description,
    }]


def parse_line_list_chunk(chunk: dict, stated_status: str, skip_prefixes: tuple = (), skip_first: int = 0) -> list[dict]:
    """Section 5 (Explicit Out-of-Scope) and 4.3's human-gate list are both, in the real docx,
    one bullet per paragraph -- chunk_gdd_by_heading() preserves paragraph breaks as '\\n' within
    a chunk's text, so each non-empty line is genuinely one bullet, not an artificial split.
    skip_first drops leading intro lines (e.g. 4.3's "Human gates -- tasks agents explicitly do
    not do:"); skip_prefixes stops collecting once a line starts a DIFFERENT kind of content within
    the same chunk (e.g. 4.3's trailing process-rule/engine-constraint paragraphs, which are policy
    notes, not human-gate bullets)."""
    lines = [ln.strip() for ln in chunk["text"].split("\n") if ln.strip()]
    lines = lines[skip_first:]
    features = []
    for line in lines:
        if any(line.startswith(p) for p in skip_prefixes):
            break
        short_name = re.split(r"[(.—-]", line)[0].strip() or line
        features.append({
            "feature": short_name,
            "section": chunk["heading"],
            "stated_status": stated_status,
            "description": line,
        })
    return features


# Section 2.6's "Status:" chunk (itself a Heading 2 literally titled "Status:") crams five
# systems' statuses into flowing prose with no per-line delimiter. Marker-anchored segmentation:
# search for each system's own proper-noun name, IN THE ORDER they actually appear in the text,
# and slice between consecutive matches. This is real code re-slicing live chunk text every run
# (not a hardcoded output) -- but the marker strings are known, hardcoded structural knowledge of
# this one paragraph, same spirit as foundation_extractor.py's gdd_documented_sequence constant.
SECTION_2_6_STATUS_MARKERS = [
    "Room progression framework",
    "Room Geometry Designer",
    "Reachability Verifier",
    "Stress Tester",
    "Known open issue",
]


def parse_section_2_6_status_chunk(chunk: dict) -> list[dict]:
    text = chunk["text"]
    positions = []
    for marker in SECTION_2_6_STATUS_MARKERS:
        idx = text.find(marker)
        if idx != -1:
            positions.append((idx, marker))
    positions.sort()
    if not positions:
        return []

    features = []
    for i, (start, marker) in enumerate(positions):
        end = positions[i + 1][0] if i + 1 < len(positions) else len(text)
        segment = _clean(text[start:end])

        if marker == "Known open issue":
            # A narrative aside describing an unresolved bug, not a "Name: status" pair like the
            # other four markers in this chunk -- the real subject (what's actually unresolved)
            # is the clause right after the marker's own parenthetical, not a proper-noun system
            # name before a colon. Named explicitly here rather than mis-extracting a truncated
            # sentence fragment as a fake "status".
            features.append({
                "feature": "Room-to-room exit trigger alignment",
                "section": f"{chunk['heading']} (nested under 2.6)",
                "stated_status": "Not started (status unconfirmed per GDD text, needs re-verification)",
                "description": segment,
            })
            continue

        # The marker's own text up to its first colon is the cleanest short status for THIS
        # system; the rest of the segment (which may narrate a second system embedded mid-
        # paragraph, e.g. Room Variation Generator inside the Room Geometry Designer segment)
        # is kept in the description so nothing is silently dropped, even though it isn't split
        # into its own top-level record here (Asset Cataloger/Room Variation Generator both get
        # their own authoritative record from 4.2's table instead -- see module docstring).
        colon_idx = segment.find(":")
        status_part = segment[colon_idx + 1:].strip() if colon_idx != -1 else segment
        features.append({
            "feature": marker,
            "section": f"{chunk['heading']} (nested under 2.6)",
            "stated_status": status_part.split(".")[0].strip() if status_part else "Not started",
            "description": segment,
        })
    return features


def extract_features(chunks: list[dict]) -> list[dict]:
    features: list[dict] = []
    seen_headings_for_status_chunk = False
    for chunk in chunks:
        heading = chunk["heading"]
        if heading == "Status:" and not seen_headings_for_status_chunk:
            seen_headings_for_status_chunk = True
            features.extend(parse_section_2_6_status_chunk(chunk))
            continue
        if heading.startswith("5."):
            features.extend(parse_line_list_chunk(chunk, "Explicit out-of-scope"))
            continue
        if heading.startswith("4.3"):
            features.extend(parse_line_list_chunk(
                chunk, "Human gate (explicit non-goal for automation)",
                skip_prefixes=("[UPDATED", "Engine-side constraint:"), skip_first=1,
            ))
            continue
        if heading.startswith("3.") or heading.startswith("1.") or heading.startswith("4.4"):
            continue  # deliberately not parsed -- see module docstring

        table_features = parse_table_chunk(chunk)
        if table_features:
            features.extend(table_features)
            features.extend(parse_stretch_goal_asides(chunk))
            continue

        single_status = parse_single_status_chunk(chunk)
        if single_status:
            features.extend(single_status)
            continue

    return features


# ================================================================================================
# STEP 2 -- codebase inventory
# ================================================================================================

_CPP_CLASS_RE = re.compile(r"class\s+\w+_API\s+(\w+)\s*:")
_CPP_SYMBOL_RE = re.compile(r"\b([A-Z][A-Za-z0-9]{3,})\b")  # PascalCase identifiers, 4+ chars
_PY_DEF_RE = re.compile(r"^(?:def|class)\s+(\w+)", re.MULTILINE)


def _normalize_token(s: str) -> str:
    return re.sub(r"[^a-z0-9]", "", s.lower())


def scan_codebase() -> dict:
    """Scans Source/PythonTest/*.h/*.cpp and Tools/*.py exactly as specified -- no other
    directories (see the README for why AgentScripts/ is deliberately NOT included here, and why
    that matters for one specific gap candidate). Returns {'files': [...], 'tokens': [(token, source)]}
    where token is a normalized (lowercase, no separators) identifier and source is a human-
    readable 'relative/path.ext' or 'relative/path.ext:SymbolName' string for evidence reporting."""
    files = []
    tokens: list[tuple] = []

    for fname in sorted(os.listdir(SOURCE_DIR)):
        if not (fname.endswith(".h") or fname.endswith(".cpp")):
            continue
        path = os.path.join(SOURCE_DIR, fname)
        files.append(f"Source/PythonTest/{fname}")
        base = os.path.splitext(fname)[0]
        tokens.append((_normalize_token(base), f"Source/PythonTest/{fname}"))
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
        for m in _CPP_CLASS_RE.finditer(text):
            tokens.append((_normalize_token(m.group(1)), f"Source/PythonTest/{fname} (class {m.group(1)})"))
        for m in _CPP_SYMBOL_RE.finditer(text):
            tokens.append((_normalize_token(m.group(1)), f"Source/PythonTest/{fname} (symbol {m.group(1)})"))

    for fname in sorted(os.listdir(TOOLS_DIR)):
        if not fname.endswith(".py") or fname == "gdd_gap_agent.py":
            continue
        path = os.path.join(TOOLS_DIR, fname)
        files.append(f"Tools/{fname}")
        base = os.path.splitext(fname)[0]
        tokens.append((_normalize_token(base), f"Tools/{fname}"))
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
        for m in _PY_DEF_RE.finditer(text):
            tokens.append((_normalize_token(m.group(1)), f"Tools/{fname} (def/class {m.group(1)})"))

    return {"files": files, "tokens": tokens}


# ================================================================================================
# STEP 3 -- gap detection
# ================================================================================================

GENERIC_STOPWORDS = {
    "a", "an", "the", "of", "in", "and", "or", "to", "for", "with", "via", "per", "not", "yet",
    "beyond", "already", "room", "level", "player", "system", "agent",
}


def significant_words(feature_name: str) -> list[str]:
    words = re.findall(r"[a-z0-9]+", feature_name.lower())
    return [w for w in words if w not in GENERIC_STOPWORDS and len(w) >= 3]


def find_gap_evidence(feature_name: str, tokens: list[tuple]) -> dict:
    """A feature counts as 'found in the codebase' if at least 2 of its distinct significant
    keywords each independently turn up (as a substring, in either direction, of some inventory
    token) -- OR, if it only has 1-2 significant keywords total, ALL of them must turn up. A
    single generic word matching one file somewhere isn't treated as real evidence a whole
    feature already exists; two or more independent hits is a much stronger signal that this
    isn't coincidental. See the module docstring for why this specific threshold (not a fuzzy
    similarity score) was chosen: the actual candidate pool this ever runs against is small
    enough (3 open, non-out-of-scope features across the whole GDD) to reason about by hand
    against this exact rule, rather than needing a generically robust NLP matcher."""
    keywords = significant_words(feature_name)
    if not keywords:
        return {"keywords": [], "hits": [], "matched": False}

    hits = []
    for kw in keywords:
        for token, source in tokens:
            if kw in token or token in kw:
                hits.append({"keyword": kw, "matched_token": token, "source": source})
                break  # one piece of evidence per keyword is enough to count it as "hit"

    distinct_hit_keywords = {h["keyword"] for h in hits}
    if len(keywords) <= 2:
        matched = len(distinct_hit_keywords) == len(keywords)
    else:
        matched = len(distinct_hit_keywords) >= 2
    return {"keywords": keywords, "hits": hits, "matched": matched}


def detect_gaps(features: list[dict], inventory: dict) -> dict:
    resolved_count = 0
    out_of_scope_count = 0
    candidates_checked = []
    gaps = []

    for feat in features:
        bucket = classify_status(feat["stated_status"])
        if bucket == "resolved":
            resolved_count += 1
            continue
        if bucket == "out_of_scope":
            out_of_scope_count += 1
            continue

        evidence = find_gap_evidence(feat["feature"], inventory["tokens"])
        candidates_checked.append({**feat, "evidence": evidence})
        if not evidence["matched"]:
            gaps.append({**feat, "evidence": evidence})

    return {
        "total_features_considered": len(features),
        "excluded_resolved": resolved_count,
        "excluded_out_of_scope": out_of_scope_count,
        "open_candidates_checked_against_codebase": candidates_checked,
        "gaps": gaps,
    }


# ================================================================================================
# STEP 4 -- prioritization
# ================================================================================================

SCHEMA_KEYWORDS = (
    "footprint", "alignment", "layer", "role", "input", "output", "schema", "position",
    "marker", "sequence", "dressing", "json",
)
REUSE_VOCAB = ("footprint", "bounds", "scale", "color", "layer", "position", "rotation", "offset")


def _extract_labeled_segment(description: str, label: str) -> str:
    """Pulls the text following 'Label: ' up to the next '. OtherLabel:' or end of string, from a
    description built by parse_table_chunk's 'Col: value. Col: value' convention. Returns '' if
    the label isn't present (e.g. this gap's description didn't come from a table row)."""
    m = re.search(rf"{label}:\s*(.*?)(?:\.\s+[A-Z][a-z]+:|$)", description, re.DOTALL)
    return m.group(1) if m else ""


def compute_priority_scores(gap: dict, all_gap_features: list[str]) -> dict:
    desc = f"{gap['feature']} {gap['description']}".lower()

    spec_clarity_score = sum(len(re.findall(rf"\b{kw}\b", desc)) for kw in SCHEMA_KEYWORDS)

    # Dependency direction matters: a feature's OWN "Input:" field is where a real dependency on
    # another gap would actually show up (it needs that other system's output to run). Searching
    # the WHOLE description instead is direction-blind -- e.g. Asset Cataloger's Role text
    # explains its purpose as "so the Room Variation Generator can draw from it", a downstream-
    # consumer mention, not an input dependency; matching against the full text would wrongly
    # flag Asset Cataloger as depending on the very feature that depends on IT.
    input_segment = _extract_labeled_segment(gap["description"], "Input").lower()
    unmet_dependencies = [
        other for other in all_gap_features
        if other.lower() != gap["feature"].lower() and other.lower() in input_segment
    ]

    reuse_score = sum(1 for kw in REUSE_VOCAB if kw in desc)

    total = spec_clarity_score - (3 * len(unmet_dependencies)) + reuse_score
    return {
        "spec_clarity_score": spec_clarity_score,
        "unmet_dependencies": unmet_dependencies,
        "reuse_score": reuse_score,
        "total_priority_score": total,
    }


def deterministic_reasoning(gap: dict, scores: dict) -> str:
    dep_clause = (
        f"it depends on {', '.join(scores['unmet_dependencies'])}, which {'is' if len(scores['unmet_dependencies']) == 1 else 'are'} still open"
        if scores["unmet_dependencies"] else "it has no unmet dependencies on other open gaps"
    )
    return (
        f"'{gap['feature']}' scores {scores['total_priority_score']} overall: "
        f"spec-clarity keyword density {scores['spec_clarity_score']} (how concretely the GDD's own "
        f"text names an input/output schema for this feature), {dep_clause}, and a reuse-signal "
        f"score of {scores['reuse_score']} (overlap between this feature's description and the "
        f"measurement vocabulary already implemented in foundation_extractor.py, this project's most-"
        f"reused module). Higher spec-clarity and reuse scores raise priority; each unmet dependency "
        f"lowers it, since a gap blocked on another not-yet-built system can't be confidently "
        f"implemented first."
    )


def try_llm_reasoning(gap: dict, scores: dict, other_gaps_summary: str) -> tuple:
    """Attempts ONE Claude API call to write the prose justification from the already-final
    scores above -- this project's own established pattern (balance_qa_reviewer.py, quip_
    generator.py's critic): the LLM writes rationale, it never re-decides the numbers. Falls back
    to deterministic_reasoning() if no ANTHROPIC_API_KEY is configured or the call fails for any
    reason -- returns (text, source) where source is 'llm' or 'deterministic_fallback (<reason>)',
    always disclosed rather than silently swapped."""
    try:
        from anthropic import Anthropic, AnthropicError
    except ImportError:
        return deterministic_reasoning(gap, scores), "deterministic_fallback (anthropic package not installed)"

    try:
        client = Anthropic()
    except AnthropicError as exc:
        return deterministic_reasoning(gap, scores), f"deterministic_fallback (no ANTHROPIC_API_KEY: {exc})"

    prompt = (
        f"Gap feature: {gap['feature']}\nGDD description: {gap['description']}\n"
        f"Computed scores (already final, do not change them): spec_clarity_score={scores['spec_clarity_score']}, "
        f"unmet_dependencies={scores['unmet_dependencies']}, reuse_score={scores['reuse_score']}, "
        f"total_priority_score={scores['total_priority_score']}.\n"
        f"Other open gaps under consideration: {other_gaps_summary}\n\n"
        "Write a 2-4 sentence prioritization justification for this gap, grounded ONLY in the "
        "numbers above (how well-specified it is, whether it has unmet dependencies, how much "
        "existing code it could reuse) -- do not invent new criteria or numbers."
    )
    try:
        response = client.messages.create(
            model="claude-sonnet-4-6",
            max_tokens=300,
            messages=[{"role": "user", "content": prompt}],
        )
        text = next(block.text for block in response.content if block.type == "text")
        return text.strip(), "llm"
    except AnthropicError as exc:
        return deterministic_reasoning(gap, scores), f"deterministic_fallback (API call failed: {exc})"


def prioritize_gaps(gaps: list[dict], use_llm: bool) -> dict:
    all_gap_features = [g["feature"] for g in gaps]
    ranked = []
    for gap in gaps:
        scores = compute_priority_scores(gap, all_gap_features)
        other_summary = "; ".join(f for f in all_gap_features if f != gap["feature"]) or "(none)"
        if use_llm:
            reasoning, source = try_llm_reasoning(gap, scores, other_summary)
        else:
            reasoning, source = deterministic_reasoning(gap, scores), "deterministic (--use-llm not set)"
        ranked.append({
            "feature": gap["feature"],
            "section": gap["section"],
            "stated_status": gap["stated_status"],
            "description": gap["description"],
            "scores": scores,
            "written_justification": reasoning,
            "reasoning_source": source,
        })

    ranked.sort(key=lambda g: g["scores"]["total_priority_score"], reverse=True)
    for i, g in enumerate(ranked):
        g["rank"] = i + 1

    return {
        "ranked_gaps": ranked,
        "selected_feature": ranked[0]["feature"] if ranked else None,
    }


# ================================================================================================
# main
# ================================================================================================

def main() -> int:
    parser = argparse.ArgumentParser(description="GDD Gap Agent -- detects and prioritizes GDD-described features missing from the codebase.")
    parser.add_argument("--gdd-path", default=GDD_PATH, help=f"Path to the GDD docx. Defaults to {GDD_PATH}")
    parser.add_argument("--features-output", default=os.path.join(TOOLS_DIR, "gdd_features.json"))
    parser.add_argument("--gaps-output", default=os.path.join(TOOLS_DIR, "gdd_gaps.json"))
    parser.add_argument("--priority-output", default=os.path.join(TOOLS_DIR, "gdd_gap_priority.json"))
    parser.add_argument("--use-llm", action="store_true", help="Attempt a Claude API call to write each gap's prose justification (falls back to a deterministic template if no ANTHROPIC_API_KEY is set).")
    args = parser.parse_args()

    print(f"STEP 1: reading GDD from {args.gdd_path}")
    chunks = chunk_gdd_by_heading(args.gdd_path)
    features = extract_features(chunks)
    with open(args.features_output, "w", encoding="utf-8") as f:
        json.dump(features, f, indent=2)
    print(f"  extracted {len(features)} features -> {args.features_output}")

    print("STEP 2: scanning codebase (Source/PythonTest/*.h/*.cpp, Tools/*.py)")
    inventory = scan_codebase()
    print(f"  scanned {len(inventory['files'])} files, {len(inventory['tokens'])} inventory tokens")

    print("STEP 3: detecting gaps")
    gap_report = detect_gaps(features, inventory)
    with open(args.gaps_output, "w", encoding="utf-8") as f:
        json.dump(gap_report, f, indent=2)
    print(f"  {gap_report['excluded_resolved']} resolved, {gap_report['excluded_out_of_scope']} out-of-scope, "
          f"{len(gap_report['open_candidates_checked_against_codebase'])} open candidates checked, "
          f"{len(gap_report['gaps'])} confirmed gap(s) -> {args.gaps_output}")
    for g in gap_report["gaps"]:
        print(f"    GAP: {g['feature']} ({g['stated_status']})")

    print("STEP 4: prioritizing gaps" + (" (LLM-assisted reasoning)" if args.use_llm else " (deterministic reasoning)"))
    priority = prioritize_gaps(gap_report["gaps"], args.use_llm)
    with open(args.priority_output, "w", encoding="utf-8") as f:
        json.dump(priority, f, indent=2)
    for g in priority["ranked_gaps"]:
        print(f"  #{g['rank']} {g['feature']} (score={g['scores']['total_priority_score']}, reasoning_source={g['reasoning_source']})")
    print(f"  -> {args.priority_output}")

    if priority["selected_feature"]:
        print(f"\nSELECTED for Step 5 code generation: {priority['selected_feature']}")
    else:
        print("\nNo open gaps found -- nothing to select for Step 5.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
