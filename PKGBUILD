# Maintainer: DeepSeek Harness desktop packaging
# Contributor: szy

pkgname=deepseek-harness-bin-desktop
pkgver=0.1.0
pkgrel=1
pkgdesc="Desktop wrapper (Qt6 WebEngine) for the DeepSeek Harness browser UI, with a bundled dsh web runtime (0.1.0-rc.6) - no browser needed"
arch=('x86_64')
url="https://github.com/PlayerSZY/deepseek-harness-desktop"
license=('MIT')
depends=('qt6-webengine' 'nodejs' 'hicolor-icon-theme')
source=("https://github.com/PlayerSZY/deepseek-harness-desktop/releases/download/v${pkgver}/deepseek-harness-bin-desktop-${pkgver}-x86_64.tar.zst")
sha256sums=('7d9569b2357d8f846241effea67d97c4b828f0838d25c4d9068411dacced8a0f')

package() {
  cd "$srcdir/deepseek-harness-desktop-${pkgver}"

  # Qt wrapper binary
  install -Dm755 bin/deepseek-harness-desktop \
    "$pkgdir/usr/lib/deepseek-harness-desktop/bin/deepseek-harness-desktop"
  install -dm755 "$pkgdir/usr/bin"
  ln -s ../lib/deepseek-harness-desktop/bin/deepseek-harness-desktop \
    "$pkgdir/usr/bin/deepseek-harness-desktop"

  # Bundled dsh web runtime (node_modules)
  cp -a --no-preserve=ownership runtime "$pkgdir/usr/lib/deepseek-harness-desktop/runtime"

  # Desktop entry
  install -Dm644 share/applications/deepseek-harness-desktop.desktop \
    "$pkgdir/usr/share/applications/deepseek-harness-desktop.desktop"

  # Icons
  for size in 16 24 32 48 64 128 256; do
    install -Dm644 "share/icons/hicolor/${size}x${size}/apps/deepseek-harness-desktop.png" \
      "$pkgdir/usr/share/icons/hicolor/${size}x${size}/apps/deepseek-harness-desktop.png"
  done
  install -Dm644 share/icons/hicolor/scalable/apps/deepseek-harness-desktop.svg \
    "$pkgdir/usr/share/icons/hicolor/scalable/apps/deepseek-harness-desktop.svg"

  # License
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
