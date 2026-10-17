# Landing page assets

Design source: [OSR-93 Figma](https://www.figma.com/design/xbtCVxX7KEJQxC8OcbCdMy).
Use the final frames and the closing OSR-93 handoff, which removes the unqualified
RSS claim from the pipeline diagram and social image.

The SVG files are unmodified Figma exports from the Logo page:

| File | Export node |
| --- | --- |
| logo-symbol-light.svg | 5:3751 |
| logo-symbol-dark.svg | 5:3759 |
| favicon-16-light.svg | 5:3799 |
| favicon-16-dark.svg | 5:3807 |
| og-image.png | 5:3514 (OG image page, final dark image, 1200 × 630) |

The symbol files also provide the 32 px favicons. The 16 px favicons use the
separate optical-size artwork. The PNG is exported directly from Figma.

Self-hosted fonts in `../fonts/`:

- Geist Regular, SemiBold and Bold from the official
  [v1.8.1 release](https://github.com/vercel/geist-font/releases/tag/v1.8.1).
  Original license: `../fonts/Geist-OFL.txt`.
- JetBrains Mono Regular, Medium and Bold from the official repository at
  [19371302b95d218af43299bce79ddbddd0bc364d](https://github.com/JetBrains/JetBrainsMono/tree/19371302b95d218af43299bce79ddbddd0bc364d/fonts/webfonts).
  Original license: `../fonts/JetBrainsMono-OFL.txt`.

The styles use the Theme, Size and Font variables from Figma. The breakpoint
and content widths follow the handoff. Text and source links remain those of
PR #16. Content-driven list heights preserve readability where the final
mobile Figma frame's fixed-height list items overlap.
