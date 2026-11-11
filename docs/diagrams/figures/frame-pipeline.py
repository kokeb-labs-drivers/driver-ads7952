"""The frame pipeline: a channel chosen in frame N comes back in frame N + 2, in Manual mode and in Auto-1.

Reads: Each column is one 16-bit frame, CS low for 16 SCLK cycles, left to right in time. In each panel the top row
is the word the driver sends on SDI, the middle row the channel the multiplexer acquires during that frame, and the
bottom row the reply on SDO: the conversion of the channel acquired in the frame before, sampled at this frame's CS
falling edge, tagged with its address in DO15:12. In Manual mode (top) ReadChannel(4) selects channel 4 with a Manual
word, sends a "continue" word and then the Manual word for channel 4 again (it carries the range once more), and
keeps the third reply, whose address must be 4; the two replies before it still belong to the previous channel. In Auto-1 (bottom), entering the mode restarts the sequence at its
first channel: with channels 2, 5 and 6 programmed, the first result arrives two frames after the Auto-1 word, and
from then on every Auto-1 word without restart (DI11 = 1, the range again) returns the next channel of the sequence,
so a pass costs one frame per channel.
The timing is the datasheet's Figures 51 and 53; the frame counts come from ReadChannel(), readSelected(), scan()
and collect() in src/ads7952.ipp.
"""

from _source import IMPL, REGISTERS, body, constants, need, read

from figkit import Fig


def data():
    reg = constants(REGISTERS)
    impl = read(IMPL)
    if reg.get("kContinue") != 0:
        raise SystemExit("figure source drift: the continue word is not 0x0000")
    need(r"stale_ = 1;", body(impl, "ADS7952<Port>::select(uint8_t channel)"), "a select leaves one reply in flight")
    need(r"frame\(reg::ManualWord\(rest, detail::ControlBits\(config_\)\)\)",
         body(impl, "ADS7952<Port>::readSelected(uint8_t channel, uint8_t rest)"), "the result is read with the Manual word again")
    need(r"address != channel", body(impl, "ADS7952<Port>::readSelected(uint8_t channel, uint8_t rest)"),
         "the address of the reply is checked")
    need(r"frame\(reg::kContinue\)", body(impl, "ADS7952<Port>::selectChecked(uint8_t channel)"),
         "a continue word follows the select")
    scan = body(impl, "ADS7952<Port>::scan(Mode mode, uint16_t mask)")
    need(r"AutoWord\(word, true, detail::ControlBits\(config_\)\)", scan, "Auto entry restarts the sequence")
    need(r"stale_ = 1;", scan, "one reply in flight after the entry")
    need(r"AutoWord\(\(mode_ == Mode::Auto2\) \? reg::kAuto2 : reg::kAuto1, false",
         body(impl, "ADS7952<Port>::collect(uint16_t mask)"), "a pass sends the Auto word without restart")
    return {"manual": {"words": ["Manual, CH4", "continue", "Manual, CH4"], "mux": ["CHy", "CH4", "CH4"],
                       "reply": ["CHx", "CHy", "CH4"], "keep": [False, False, True]},
            "auto": {"words": ["Auto-1, restart", "continue", "Auto-1", "Auto-1", "Auto-1"],
                     "mux": ["CHy", "CH2", "CH5", "CH6", "CH2"], "reply": ["CHx", "CHy", "CH2", "CH5", "CH6"],
                     "keep": [False, False, True, True, True]},
            "sources": [f"{IMPL}:select()", f"{IMPL}:selectChecked()", f"{IMPL}:readSelected()", f"{IMPL}:scan()", f"{REGISTERS}:kContinue"]}


def _panel(f, y, title, p, cw):
    k = f.k
    lab = 190
    f.text(0, y + k["card_title"], title, size=k["card_title"], weight=700)
    y += k["card_title"] + 16
    for i in range(len(p["words"])):
        f.text(lab + i * cw + cw / 2 - 6, y + k["small"], f"frame N{'' if i == 0 else f' + {i}'}", size=k["small"],
               color="muted", anchor="middle")
    y += k["small"] + 10
    rows = [("SDI: the driver sends", "words", "brand"), ("Multiplexer acquires", "mux", "slate"),
            ("SDO: the reply", "reply", "teal")]
    bh = 46
    for name, key, tone in rows:
        f.text(0, y + bh / 2 + k["card_body"] * 0.36, name, size=k["card_body"], weight=600)
        for i, s in enumerate(p[key]):
            t = tone
            if key == "reply":
                t = "amber" if p["keep"][i] else "slate"
            f.rect(lab + i * cw, y, cw - 12, bh, fill=f"{t}.tint", stroke=f"{t}.strong", rx=6, sw=1.2)
            f.text(lab + i * cw + (cw - 12) / 2, y + bh / 2 + k["card_body"] * 0.36, s, size=k["card_body"],
                   weight=600, color=f"{t}.text", bg=f"{t}.tint", anchor="middle")
        y += bh + 12
    return y


def build(d):
    f = Fig(
        "frame-pipeline",
        "A result comes back two frames after its channel",
        alt=("Two timing grids of 16-bit frames. Manual: a Manual word selects channel 4 in frame N, the multiplexer "
             "acquires it in frame N + 1 and its result comes back in frame N + 2. Auto-1: the Auto-1 word restarts "
             "the sequence 2, 5, 6 in frame N, and from frame N + 2 on every Auto-1 word returns the next channel."),
        width=1200,
        kicker="Architecture",
        subtitle="One column per frame. The amber replies are the ones the driver returns; the grey ones it skips.",
        legend=[("brand", "word on SDI"), ("slate", "acquired, or a reply skipped"), ("amber", "reply returned")],
    )
    cw = 190
    y = _panel(f, 0, "Manual: ReadChannel(4), 3 frames (1 when channel 4 is already selected)", d["manual"], cw)
    y = _panel(f, y + 24, "Auto-1, channels 2, 5, 6: ReadAuto1(), 3 + 2 frames to enter, then 3", d["auto"], cw)
    f.note(0, y + 12, f.body_w,
           "Every reply carries its channel address in DO15:12. The driver checks it on every result it returns, so "
           "a lost frame, a reset chip or a dead SDI line is an error, never a result for the wrong channel.",
           tone="brand", title="Keyed on the address")
    return f
