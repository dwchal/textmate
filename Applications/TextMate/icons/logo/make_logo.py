#!/usr/bin/env python3
"""Generates logo.svg, the TextMate (Opportunistic Labs build) app icon.

A gear, as in the fork's first icon, with a text cursor where the axle would
be. Colours are the Opportunistic Labs palette. Run `./build_icon` afterwards
to regenerate TextMate.icns and the PNGs used by the About window and README.
"""
import math

SIZE   = 1024
CX, CY = 512, 512
TEETH  = 8
R_TIP, R_ROOT, R_HOLE = 300, 232, 146

def polar(r, deg):
    a = math.radians(deg)
    return CX + r * math.cos(a), CY + r * math.sin(a)

def gear_path():
    step = 360 / TEETH
    tip, root = step * 0.26, step * 0.44      # angular width of a tooth at its tip and at its root
    d = []
    for i in range(TEETH):
        c = i * step - 90
        pts = [(R_ROOT, c - root / 2), (R_TIP, c - tip / 2), (R_TIP, c + tip / 2), (R_ROOT, c + root / 2)]
        (x0, y0), (x1, y1), (x2, y2), (x3, y3) = [polar(r, a) for r, a in pts]
        d.append(("M" if i == 0 else "L") + f"{x0:.2f},{y0:.2f}")
        d.append(f"L{x1:.2f},{y1:.2f}")
        d.append(f"A{R_TIP},{R_TIP} 0 0 1 {x2:.2f},{y2:.2f}")
        d.append(f"L{x3:.2f},{y3:.2f}")
        nx, ny = polar(R_ROOT, c + step - root / 2)
        d.append(f"A{R_ROOT},{R_ROOT} 0 0 1 {nx:.2f},{ny:.2f}")
    d.append("Z")
    # the hole, wound the other way so that the non-zero fill rule leaves it open
    d.append(f"M{CX + R_HOLE},{CY} A{R_HOLE},{R_HOLE} 0 1 0 {CX - R_HOLE},{CY} A{R_HOLE},{R_HOLE} 0 1 0 {CX + R_HOLE},{CY} Z")
    return " ".join(d)

SVG = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}" width="{SIZE}" height="{SIZE}">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#1b5a96"/>
      <stop offset="0.55" stop-color="#0f3a63"/>
      <stop offset="1" stop-color="#0b263f"/>
    </linearGradient>
    <radialGradient id="glow" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0" stop-color="#2aa9a6" stop-opacity="0.55"/>
      <stop offset="1" stop-color="#2aa9a6" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="gear" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ffffff"/>
      <stop offset="1" stop-color="#bfe8e5"/>
    </linearGradient>
    <linearGradient id="cursor" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#9ff7ec"/>
      <stop offset="1" stop-color="#3ecfc6"/>
    </linearGradient>
    <linearGradient id="hub" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#0b263f"/>
      <stop offset="1" stop-color="#14456f"/>
    </linearGradient>
    <linearGradient id="sheen" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.22"/>
      <stop offset="0.5" stop-color="#ffffff" stop-opacity="0"/>
    </linearGradient>
    <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="14" stdDeviation="18" flood-color="#04121f" flood-opacity="0.45"/>
    </filter>
    <filter id="soft" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="8" stdDeviation="10" flood-color="#04121f" flood-opacity="0.35"/>
    </filter>
  </defs>

  <!-- macOS icon shape: 824 pt rounded square on a 1024 pt canvas -->
  <rect x="100" y="100" width="824" height="824" rx="186" fill="url(#bg)" filter="url(#shadow)"/>
  <rect x="100" y="100" width="824" height="824" rx="186" fill="url(#sheen)"/>
  <circle cx="{CX}" cy="{CY}" r="330" fill="url(#glow)"/>

  <!-- gear: the round stroke softens every corner -->
  <path d="{gear_path()}" fill="url(#gear)" stroke="url(#gear)" stroke-width="26" stroke-linejoin="round" filter="url(#soft)"/>

  <!-- hub, and a text cursor in place of the axle -->
  <circle cx="{CX}" cy="{CY}" r="{R_HOLE - 13}" fill="url(#hub)"/>
  <g fill="url(#cursor)">
    <rect x="{CX - 17}" y="{CY - 96}" width="34" height="192" rx="17"/>
    <rect x="{CX - 52}" y="{CY - 104}" width="104" height="30" rx="15"/>
    <rect x="{CX - 52}" y="{CY + 74}" width="104" height="30" rx="15"/>
  </g>
</svg>
'''

open("logo.svg", "w").write(SVG)
