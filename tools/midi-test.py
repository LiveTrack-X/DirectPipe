#!/usr/bin/env python3
"""
DirectPipe MIDI Test CLI
========================
Sends simulated MIDI messages to DirectPipe via HTTP API.
No external MIDI hardware or virtual drivers needed.

Usage:
  python midi-test.py cc <channel> <cc#> <value>     Send CC message
  python midi-test.py note <channel> <note#> [vel]    Send Note On message
  python midi-test.py learn cc <channel> <cc#> <value>  Trigger Learn + send CC
  python midi-test.py learn note <channel> <note#>      Trigger Learn + send Note
  python midi-test.py status                            Show current state
  python midi-test.py interactive                       Interactive mode

Examples:
  python midi-test.py cc 1 7 127          # CC#7 Ch1 Value=127
  python midi-test.py cc 1 7 0            # CC#7 Ch1 Value=0
  python midi-test.py note 1 60           # Note 60 Ch1 Vel=127
  python midi-test.py note 1 60 100       # Note 60 Ch1 Vel=100
  python midi-test.py interactive         # Interactive control panel

Requires: DirectPipe running with HTTP API enabled (port 8766)
"""

import sys
import json
import time
try:
    from urllib.request import urlopen, Request
    from urllib.error import URLError
except ImportError:
    print("Python 3 required")
    sys.exit(1)

BASE_URL = "http://localhost:8766"


def api_get(path):
    """Send GET request to DirectPipe HTTP API."""
    url = f"{BASE_URL}{path}"
    try:
        resp = urlopen(Request(url), timeout=3)
        data = resp.read().decode("utf-8")
        return json.loads(data)
    except URLError as e:
        print(f"Error: Cannot connect to DirectPipe ({e})")
        print("Make sure DirectPipe is running and HTTP API is enabled.")
        return None
    except json.JSONDecodeError:
        print("Error: Invalid response from DirectPipe")
        return None


def send_cc(channel, cc_num, value):
    """Send a CC message."""
    result = api_get(f"/api/midi/cc/{channel}/{cc_num}/{value}")
    if result and result.get("ok"):
        print(f"OK: CC#{cc_num} Ch{channel} Value={value}")
    elif result:
        print(f"Error: {result.get('error', 'Unknown')}")
    return result


def send_note(channel, note_num, velocity=127):
    """Send a Note On message."""
    result = api_get(f"/api/midi/note/{channel}/{note_num}/{velocity}")
    if result and result.get("ok"):
        print(f"OK: Note {note_num} Ch{channel} Vel={velocity}")
    elif result:
        print(f"Error: {result.get('error', 'Unknown')}")
    return result


def show_status():
    """Show current DirectPipe state."""
    result = api_get("/api/status")
    if result:
        print(json.dumps(result, indent=2))
    return result


def interactive_mode():
    """Interactive MIDI test console."""
    print("DirectPipe MIDI Test - Interactive Mode")
    print("=" * 45)
    print("Commands:")
    print("  cc <ch> <num> <val>   Send CC (e.g., cc 1 7 127)")
    print("  note <ch> <num> [vel] Send Note (e.g., note 1 60)")
    print("  sweep <ch> <cc> [ms]  Sweep CC 0→127→0 (e.g., sweep 1 7 50)")
    print("  status                Show state")
    print("  q / quit              Exit")
    print()

    # Verify connection
    result = api_get("/api/status")
    if not result:
        return
    print("Connected to DirectPipe!\n")

    while True:
        try:
            line = input("midi> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not line:
            continue

        parts = line.split()
        cmd = parts[0].lower()

        if cmd in ("q", "quit", "exit"):
            break
        elif cmd == "status":
            show_status()
        elif cmd == "cc" and len(parts) >= 4:
            try:
                ch, num, val = int(parts[1]), int(parts[2]), int(parts[3])
                send_cc(ch, num, val)
            except ValueError:
                print("Usage: cc <channel 1-16> <cc# 0-127> <value 0-127>")
        elif cmd == "note" and len(parts) >= 3:
            try:
                ch, num = int(parts[1]), int(parts[2])
                vel = int(parts[3]) if len(parts) >= 4 else 127
                send_note(ch, num, vel)
            except ValueError:
                print("Usage: note <channel 1-16> <note# 0-127> [velocity 0-127]")
        elif cmd == "sweep" and len(parts) >= 3:
            try:
                ch, cc_num = int(parts[1]), int(parts[2])
                delay_ms = int(parts[3]) if len(parts) >= 4 else 30
                delay_s = delay_ms / 1000.0
                print(f"Sweeping CC#{cc_num} Ch{ch} 0→127→0 ({delay_ms}ms/step)...")
                for v in range(0, 128, 4):
                    send_cc(ch, cc_num, v)
                    time.sleep(delay_s)
                for v in range(127, -1, -4):
                    send_cc(ch, cc_num, v)
                    time.sleep(delay_s)
                print("Sweep done.")
            except ValueError:
                print("Usage: sweep <channel> <cc#> [delay_ms]")
        else:
            print("Unknown command. Type 'cc', 'note', 'sweep', 'status', or 'q'.")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(0)

    cmd = sys.argv[1].lower()

    if cmd == "cc" and len(sys.argv) >= 5:
        ch, num, val = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
        send_cc(ch, num, val)

    elif cmd == "note" and len(sys.argv) >= 4:
        ch, num = int(sys.argv[2]), int(sys.argv[3])
        vel = int(sys.argv[4]) if len(sys.argv) >= 5 else 127
        send_note(ch, num, vel)

    elif cmd == "status":
        show_status()

    elif cmd == "interactive":
        interactive_mode()

    elif cmd == "learn" and len(sys.argv) >= 3:
        # Send a message that triggers MIDI Learn (if active in UI)
        subcmd = sys.argv[2].lower()
        if subcmd == "cc" and len(sys.argv) >= 6:
            ch, num, val = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
            print("Sending CC (will be captured if Learn mode active)...")
            send_cc(ch, num, val)
        elif subcmd == "note" and len(sys.argv) >= 5:
            ch, num = int(sys.argv[3]), int(sys.argv[4])
            print("Sending Note (will be captured if Learn mode active)...")
            send_note(ch, num)
        else:
            print("Usage: midi-test.py learn cc <ch> <num> <val>")
            print("       midi-test.py learn note <ch> <num>")
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
