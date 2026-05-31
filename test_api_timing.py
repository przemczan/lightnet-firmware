#!/usr/bin/env python3
"""
Test Lightnet API timing and performance.
Run this on your local machine and monitor the controller's serial output simultaneously.
"""

import requests
import time
import json
from datetime import datetime

BASE_URL = "http://10.1.1.239"
ENDPOINTS = [
    ("/api/palettes", "GET", None),
    ("/api/palettes/userColors", "GET", None),
    ("/api/scenes", "GET", None),
    ("/api/scenes/status", "GET", None),
]

def format_time(ms):
    """Format milliseconds nicely."""
    if ms < 1000:
        return f"{ms:.0f}ms"
    return f"{ms/1000:.2f}s"

def test_endpoint(method, url, data=None):
    """Test a single endpoint and measure timing."""
    full_url = BASE_URL + url

    try:
        # Measure time from request start to response complete
        start = time.perf_counter()

        if method == "GET":
            response = requests.get(full_url, timeout=10)
        elif method == "POST":
            response = requests.post(full_url, json=data, timeout=10)
        else:
            raise ValueError(f"Unknown method: {method}")

        elapsed = (time.perf_counter() - start) * 1000  # Convert to ms

        # Try to parse response
        try:
            body = response.json() if response.text else {}
        except:
            body = response.text[:100]  # First 100 chars

        return {
            "status": response.status_code,
            "elapsed_ms": elapsed,
            "content_length": len(response.content),
            "body": body,
            "success": response.status_code == 200,
            "error": None
        }

    except requests.exceptions.Timeout:
        return {"error": "TIMEOUT (>10s)", "elapsed_ms": 10000, "success": False}
    except requests.exceptions.ConnectionError as e:
        return {"error": f"CONNECTION FAILED: {e}", "success": False}
    except Exception as e:
        return {"error": str(e), "success": False}

def main():
    print("=" * 70)
    print(f"Lightnet API Timing Test - {datetime.now().strftime('%H:%M:%S')}")
    print(f"Target: {BASE_URL}")
    print("=" * 70)
    print("\nMake sure to monitor the controller's serial output!")
    print("You should see timestamps like: [PaletteServer::handleListPalettes] TOTAL: XXX ms")
    print("\n" + "=" * 70 + "\n")

    for endpoint, method, data in ENDPOINTS:
        print(f"{method:4} {endpoint:30}", end=" ... ", flush=True)

        result = test_endpoint(method, endpoint, data)

        if result.get("success"):
            elapsed = result["elapsed_ms"]
            size = result["content_length"]
            print(f"✓ {format_time(elapsed):8} ({size} bytes)")
        elif result.get("error"):
            print(f"✗ {result['error']}")
        else:
            elapsed = result.get("elapsed_ms", 0)
            print(f"✗ HTTP {result['status']} ({format_time(elapsed)})")

        time.sleep(0.5)  # Pause between requests

    print("\n" + "=" * 70)
    print("\nNow run multiple requests in sequence to see if timing is consistent:")
    print("\n" + "=" * 70 + "\n")

    # Run multiple /api/palettes requests
    for i in range(5):
        print(f"Request {i+1:2}: GET /api/palettes ... ", end="", flush=True)
        result = test_endpoint("GET", "/api/palettes")

        if result.get("success"):
            elapsed = result["elapsed_ms"]
            print(f"✓ {format_time(elapsed)}")
        else:
            print(f"✗ {result.get('error', 'Failed')}")

        time.sleep(0.5)

    print("\n" + "=" * 70)
    print("\nExpected vs Actual:")
    print("  - Firmware handler time:     ~280-300ms (from serial logs)")
    print("  - Network/WiFi overhead:     varies with WiFi conditions")
    print("  - Total client time:         handler + overhead")
    print("\nIf total is significantly higher than handler time,")
    print("it means WiFi transmission or AsyncWebServer buffering is slow.")
    print("=" * 70)

if __name__ == "__main__":
    main()
