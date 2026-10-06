#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
vip_test_client.py - standalone bring-up/test tool for the 5E-HTE firmware,
speaking the exact same VIP(outer)+kria(inner) binary protocol as
App/backend/vip_helper.cpp + kria_helper.h and the firmware's
src/vip_helper.c + src/kria_helper.h (verified byte-identical - see
README.md "Protocol cross-check").

Use this to exercise the firmware over RS422/USB-serial BEFORE the Qt app's
own VIP wiring (Phase 2) is done, or any time you want a scriptable way to
send commands / watch ADC7928 telemetry without launching the full app.

Requires: pip install pyserial

Examples:
    python3 vip_test_client.py --port /dev/ttyUSB0 --baud 115200 ac 220 50 1
    python3 vip_test_client.py --port COM5 dc 24 1.5 1
    python3 vip_test_client.py --port COM5 att 2 15
    python3 vip_test_client.py --port COM5 delay 270
    python3 vip_test_client.py --port COM5 pwq ac        # read AC supply now
    python3 vip_test_client.py --port COM5 raw ac "PROT:CLE"   # raw command to AC
    python3 vip_test_client.py --port COM5 ping
    python3 vip_test_client.py --port COM5 listen        # just print frames
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("pip install pyserial", file=sys.stderr)
    raise

VIP_SYNC = 0xEB90
VIP_HEADER_LEN = 9
FRAME_DATA_STD = 10
FRAME_PING_STD = 11
FRAME_ACK_STD = 12

NODE_HOST = 0x00
NODE_FW = 0x01


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def crc32_ieee(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF


def kria_encode(seq: int, msgid: int, payload: bytes) -> bytes:
    return bytes([seq & 0xFF, msgid & 0xFF, len(payload) & 0xFF]) + payload


def vip_encode_data(des_id: int, src_id: int, frame_seq: int, inner: bytes) -> bytes:
    header = struct.pack("<BBBHHBB", FRAME_DATA_STD, VIP_HEADER_LEN, 0,
                          frame_seq & 0xFFFF, len(inner) & 0xFFFF, des_id, src_id)
    hcrc = crc16_ccitt_false(header)
    body = header + struct.pack("<H", hcrc) + inner
    mcrc = crc32_ieee(inner)
    body += struct.pack("<I", mcrc)
    return struct.pack("<H", VIP_SYNC) + body


def vip_encode_ping(des_id: int, src_id: int, frame_seq: int) -> bytes:
    header = struct.pack("<BBBHHBB", FRAME_PING_STD, VIP_HEADER_LEN, 0x01,
                          frame_seq & 0xFFFF, 0, des_id, src_id)
    hcrc = crc16_ccitt_false(header)
    return struct.pack("<H", VIP_SYNC) + header + struct.pack("<H", hcrc)


# ---- fwi_msg_t / timedelay_msg_t / annuation_msg_t / cmd_msg_t layouts ----
# Must stay byte-identical to App/backend/kria_helper.h and
# firmware/src/kria_helper.h (#pragma pack(push, 1) on both sides).

def build_fwi(is_ac: str, voltage: int, freq_or_current: int, state: int) -> bytes:
    msg = b"FWI\x00"
    isac = is_ac.encode("ascii")[:2].ljust(3, b"\x00")
    return msg + isac + struct.pack("<HHB", voltage & 0xFFFF, freq_or_current & 0xFFFF, state & 0xFF)


def build_delay(ns: int) -> bytes:
    return b"DEL\x00" + struct.pack("<H", ns & 0xFFFF)


def build_att(channel: int, value_db: float) -> bytes:
    """annuation_q_msg_t (7 bytes): value = number of 0.25 dB steps (0..127).
    Same encoding as the Qt app's onAttUpdateRequested()."""
    steps = max(0, min(127, int(round(value_db * 4))))
    return b"ATQ\x00" + struct.pack("<BH", channel & 0xFF, steps)


def build_att_legacy(channel: int, value_db: int) -> bytes:
    """Old 6-byte annuation_msg_t (whole dB) - still accepted by the firmware."""
    return b"ATT\x00" + bytes([channel & 0xFF, value_db & 0xFF])


def build_cmd(name: str, values):
    name_b = name.encode("ascii")[:63].ljust(64, b"\x00")
    vals = list(values)[:3] + [0xFF]
    vals = (vals + [0xFF, 0xFF, 0xFF, 0xFF])[:4]
    return name_b + bytes(v & 0xFF for v in vals)


def build_pwq(dev: str) -> bytes:
    """msgid 41 pwr_query_msg_t: "PWQ\\0", dev (1=AC, 2=DC), flags=0."""
    return b"PWQ\x00" + struct.pack("<BB", 1 if dev.lower() == "ac" else 2, 0)


PWR_RESULTS = {0: "OK", 1: "NO_REPLY", 2: "DEV_ERROR", 3: "OUT_MISMATCH",
               4: "NOT_READY", 5: "ABORTED", 6: "BAD_REPLY", 7: "BUSY"}


def build_raw(dev: str, text: str) -> bytes:
    """msgid 43 pwr_raw_msg_t: "RAW\\0", dev, len, text[64] (NUL padded)."""
    t = text.encode("ascii")[:63]
    return b"RAW\x00" + struct.pack("<BB", 1 if dev == "ac" else 2, len(t)) + t.ljust(64, b"\x00")
PWR_REASONS = {0: "READ", 1: "SET", 2: "RESET"}


def parse_vip_stream(buf: bytearray):
    """Very small non-streaming parser for printing whatever comes back
    (good enough for a bring-up tool; not the production decoder)."""
    frames = []
    i = 0
    while i + 2 <= len(buf):
        if buf[i] == 0x90 and i + 1 < len(buf) and buf[i + 1] == 0xEB:
            if i + 2 + VIP_HEADER_LEN + 2 > len(buf):
                break
            header = buf[i + 2:i + 2 + VIP_HEADER_LEN]
            frame_type, header_size, control, frame_seq, data_len, des_id, src_id = \
                struct.unpack("<BBBHHBB", header)
            hcrc_off = i + 2 + VIP_HEADER_LEN
            total = 2 + VIP_HEADER_LEN + 2 + (data_len + 4 if data_len else 0)
            if i + total > len(buf):
                break
            payload = None
            if data_len:
                p_off = hcrc_off + 2
                payload = bytes(buf[p_off:p_off + data_len])
            frames.append((frame_type, des_id, src_id, frame_seq, data_len, payload))
            i += total
        else:
            i += 1
    del buf[:i]
    return frames


def describe_frame(f):
    frame_type, des_id, src_id, frame_seq, data_len, payload = f
    label = {FRAME_DATA_STD: "DATA", FRAME_PING_STD: "PING", FRAME_ACK_STD: "ACK"}.get(frame_type, "?")
    line = f"[{label}] des={des_id} src={src_id} seq={frame_seq} len={data_len}"
    if payload and len(payload) >= 3:
        seq, msgid, plen = payload[0], payload[1], payload[2]
        body = payload[3:3 + plen]
        line += f" inner(seq={seq} msgid={msgid} len={plen} raw={body.hex()})"
        if msgid == 40 and len(body) >= 4 + 16:
            codes = struct.unpack("<8H", body[4:4 + 16])
            line += f" ADC7928 codes={codes}"
        elif msgid == 42 and len(body) >= 24:
            dev, reason, result, out, valid, req_seq, err, mv, ma, mhz = struct.unpack("<BBBBBBhiii", body[4:24])
            outs = {0: "OFF", 1: "ON"}.get(out, "?")
            line = line.split(" raw=")[0] + (f" PWS dev={'AC' if dev == 1 else 'DC'} reason={PWR_REASONS.get(reason, reason)}"
                                             f" result={PWR_RESULTS.get(result, result)} out={outs} valid=0x{valid:02x}"
                                             f" err={err} V={mv/1000:.3f} I={ma/1000:.3f} F={mhz/1000:.3f})")
        elif msgid in (35, 36, 37, 38, 41, 43) and len(body) >= 1:
            line += f" ACK success={body[0]}"
        elif msgid == 39 and body[:4] == b"WAVE":
            mags = struct.unpack("<%dI" % ((len(body) - 4) // 4), body[4:4 + ((len(body) - 4) // 4) * 4])
            k = max(range(len(mags)), key=lambda i: mags[i]) if mags else 0
            line = line.split(" raw=")[0] + f" WAVE n={len(mags)} peak_bin={k} |X|={mags[k] if mags else 0})"
        elif msgid == 39 and body[:4] == b"WAVD" and len(body) >= 6:
            cnt = body[4]
            vals = struct.unpack("<%dh" % cnt, body[6:6 + 2 * cnt])
            k = max(range(cnt), key=lambda i: vals[i]) if cnt else 0
            line = line.split(" inner(")[0] + f" WAVD n={cnt} peak_bin={k} ({vals[k] / 100:.2f} dBFS)"
        elif msgid == 44 and len(body) >= 11:
            dev, res, out, valid, err, ln = struct.unpack("<BBBBhB", body[4:11])
            txt = body[11:11 + ln].decode("ascii", "replace")
            line = line.split(" inner(")[0] + (f" RAW-REPLY dev={'AC' if dev == 1 else 'DC'} result={PWR_RESULTS.get(res, res)}"
                                              f" reply='{txt}'" + (f" err={err}" if valid & 0x10 else "")
                                              + (f" out={'ON' if out == 1 else 'OFF'}" if valid & 0x08 else ""))
        elif msgid == 39 and body[:4] == b"LOGT" and len(body) >= 5:
            lvl = {0: "INFO", 1: "WARN", 2: "LOI"}.get(body[4], "?")
            line = line.split(" inner(")[0] + f" BOARD-LOG [{lvl}] " + body[5:].decode("utf-8", "replace")
        elif msgid == 39 and body[:4] == b"DIAG" and len(body) >= 76:
            (ver, ad_ok, rf_ok, adc_ok, att_ok, odly_ok, ac_ok, dc_ok, ad_st, up, d_ok, d_to, d_sf,
             peak, _res, pc_b, pc_ok, pc_bad, pc_drop) = struct.unpack("<8BiIIIIHHIIII", body[4:52])
            build = body[52:76].split(b"\0")[0].decode("ascii", "replace")
            line = line.split(" inner(")[0] + (f" DIAG fw='{build}' up={up}s AD9361={'OK' if ad_ok else 'LOI(%d)' % ad_st}"
                                              f" RFstream={'OK' if rf_ok else 'TAT'} DMA ok={d_ok} timeout={d_to} startfail={d_sf}"
                                              f" RX1peak={peak}/2047 PC rx={pc_b}B frames ok={pc_ok} bad={pc_bad} drop={pc_drop}"
                                              f" ATT={att_ok} DLY={odly_ok} ADC={adc_ok} AC={ac_ok} DC={dc_ok}")
        elif msgid == 39 and body[:4] == b"RFST" and len(body) >= 28:
            lo, fs, bw, gain, mode, nfft, bits, win = struct.unpack("<QIIiBBBB", body[4:28])
            line = line.split(" raw=")[0] + f" RFST LO={lo/1e6:.3f}MHz Fs={fs/1e6:.3f}MSPS BW={bw/1e6:.3f}MHz gain={gain}dB mode={mode} fft={nfft})"
    return line


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=1.0)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("ac", help="AC volts freq state")
    p.add_argument("volts", type=int)
    p.add_argument("freq", type=int)
    p.add_argument("state", type=int)

    p = sub.add_parser("dc", help="DC volts current_A state (current sent x100, e.g. 2.35)")
    p.add_argument("volts", type=int)
    p.add_argument("current", type=float)
    p.add_argument("state", type=int)

    p = sub.add_parser("att", help="ATT channel(1-3) value_db(0-31.75, 0.25 dB steps)")
    p.add_argument("channel", type=int)
    p.add_argument("value_db", type=float)

    p = sub.add_parser("att_legacy", help="ATT channel(1-3) value_db(0-31) - old whole-dB 6-byte format")
    p.add_argument("channel", type=int)
    p.add_argument("value_db", type=int)

    p = sub.add_parser("delay", help="Optical delay ns: 0/100/270/400")
    p.add_argument("ns", type=int)

    p = sub.add_parser("cmd", help="Generic command: name [v1 v2 v3]")
    p.add_argument("name")
    p.add_argument("values", nargs="*", type=int, default=[])

    p = sub.add_parser("raw", help="Raw text command to a supply (msgid 43): raw ac \"MEAS:POW?\"")
    p.add_argument("dev", choices=["ac", "dc"])
    p.add_argument("text")

    p = sub.add_parser("pwq", help="Read one supply now (msgid 41): ac | dc")
    p.add_argument("dev", choices=["ac", "dc"])

    sub.add_parser("ping", help="Send a VIP PING_STD and wait for ACK")
    sub.add_parser("listen", help="Just print any VIP frames received")

    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=args.timeout)
    seq = 1

    if args.cmd == "ac":
        inner = kria_encode(seq, 35, build_fwi("AC", args.volts, args.freq, args.state))
        frame = vip_encode_data(NODE_FW, NODE_HOST, seq, inner)
    elif args.cmd == "dc":
        inner = kria_encode(seq, 36, build_fwi("DC", args.volts, int(round(args.current * 100)), args.state))
        frame = vip_encode_data(NODE_FW, NODE_HOST, seq, inner)
    elif args.cmd == "att":
        inner = kria_encode(seq, 38, build_att(args.channel, args.value_db))
        frame = vip_encode_data(NODE_FW, NODE_HOST, seq, inner)
    elif args.cmd == "att_legacy":
        inner = kria_encode(seq, 38, build_att_legacy(args.channel, args.value_db))
        frame = vip_encode_data(NODE_FW, NODE_HOST, seq, inner)
    elif args.cmd == "delay":
        inner = kria_encode(seq, 37, build_delay(args.ns))
        frame = vip_encode_data(NODE_FW, NODE_HOST, seq, inner)
    elif args.cmd == "cmd":
        inner = kria_encode(seq, 39, build_cmd(args.name, args.values))
        frame = vip_encode_data(NODE_FW, NODE_HOST, seq, inner)
    elif args.cmd == "raw":
        inner = kria_encode(seq, 43, build_raw(args.dev, args.text))
        frame = vip_encode_data(NODE_FW, NODE_HOST, seq, inner)
    elif args.cmd == "pwq":
        inner = kria_encode(seq, 41, build_pwq(args.dev))
        frame = vip_encode_data(NODE_FW, NODE_HOST, seq, inner)
    elif args.cmd == "ping":
        frame = vip_encode_ping(NODE_FW, NODE_HOST, seq)
    elif args.cmd == "listen":
        frame = None
    else:
        ap.error("unknown command")
        return

    if frame is not None:
        print("TX:", frame.hex())
        ser.write(frame)

    buf = bytearray()
    deadline = time.time() + (5.0 if args.cmd != "listen" else 3600.0)
    while time.time() < deadline:
        chunk = ser.read(256)
        if chunk:
            buf.extend(chunk)
            for f in parse_vip_stream(buf):
                print("RX:", describe_frame(f))
        elif args.cmd != "listen":
            break


if __name__ == "__main__":
    main()
