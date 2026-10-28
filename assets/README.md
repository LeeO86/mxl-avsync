# App icon source and generated variants

Edit `app-icon.svg`, then run `npm ci && npm run render` in this directory.
The pinned resvg renderer and PNG encoder regenerate every committed icon.
`generated/*.svg` are derived views, not separate editable designs.

The old inner 206-pixel panel fills the new 256-pixel design. The artwork is
24.27% larger, with no transparent padding or outer dark tile around it.

- iOS `mobile-app/assets/icon.png`: opaque 1024-pixel RGB square, full background,
  no drawn outside frame or rounded mask. iOS supplies the mask and system
  lighting/bevel. `web/apple-touch-icon.png` uses the same launcher treatment.
- Content mark `web/icon.png` and `mobile-app/assets/header-icon.png`: the blue
  rounded boundary is present, with transparent corners. The header uses 44 pt
  to balance the 44-pt settings target. The test-picture renderer uses web/icon.
- Android adaptive foreground: transparent artwork without background/frame,
  automatically fit inside the centered 66/108 safe circle. App config supplies
  the solid background; Android controls the mask. A separate monochrome outline
  supports themed icons. `android-icon.png` is the framed legacy fallback.
- Both web favicon sizes use the same framed content mark.

The generator asserts opaque iOS pixels and Android safe-zone bounds. Android
icon assets are prepared; this does not implement or validate Android capture.
Apple system highlights are not baked into any source file and are controlled
by iOS. The source font/wordmark is unchanged; there is no text in launcher art.
