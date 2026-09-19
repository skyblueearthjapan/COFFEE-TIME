# Codex の画像生成で COFFEE TIME の背景画像を 1 枚作る
# 使い方: powershell -File tools/gen_bg.ps1 <出力名> "<雰囲気の説明>"
param(
    [Parameter(Mandatory)] [string] $Name,
    [Parameter(Mandatory)] [string] $Mood
)

Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force assets\backgrounds | Out-Null

$prompt = @"
Use your image generation tool (use the gpt-image-2.5 model if available; otherwise the newest image model you have) to create ONE photorealistic background image, resize it to exactly 1024x1024, and save it as PNG to assets/backgrounds/$Name.png in this repository. Do not modify or create any other files in the repository.

Purpose: full-screen background for a 480x480 ROUND LCD on a coffee-counter device in an office cafe corner. Cream/white UI is drawn ON TOP of it: a big clock and weather line in the top half, a round brown "+1" button in the lower center, and small numbers at the left and right of the button.

Composition rules (important):
- Square 1:1. Everything must read well inside the inscribed circle; the corners are cropped.
- NO single strong subject in the center or lower center (that area is covered by the button). Place any coffee cup, coffee beans, pot or plant only partially at the far left/right EDGES or bottom edge, softly out of focus.
- The center and top half should be calm, blurred and low-detail (bokeh, soft light, wall/window blur, gentle steam).
- Overall dark and low-contrast (roughly 20-35% brightness) so white text stays readable. Warm cafe color palette (browns, amber, cream) with a touch of green.
- No text, no letters, no logos, no watermarks, no UI.

Mood / time of day: $Mood

After saving, print the exact file path and which image model was used.
"@

codex exec -s workspace-write $prompt 2>&1 | Select-Object -Last 6
