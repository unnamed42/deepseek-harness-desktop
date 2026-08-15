# Maintainer: DeepSeek Harness desktop packaging
# Contributor: szy

pkgname=deepseek-harness-bin-desktop
pkgver=0.2.0
pkgrel=2
pkgdesc="Desktop wrapper (Qt6 WebEngine) for the DeepSeek Harness browser UI - uses the deepseek-harness-bin dsh backend via a systemd user service, no browser needed"
arch=('x86_64')
url="https://github.com/PlayerSZY/deepseek-harness-desktop"
license=('MIT')
depends=('qt6-webengine' 'deepseek-harness-bin' 'hicolor-icon-theme')
source=("https://github.com/PlayerSZY/deepseek-harness-desktop/releases/download/v${pkgver}/deepseek-harness-bin-desktop-${pkgver}-x86_64.tar.zst")
sha256sums=('a741783d98719239d3d67b9d410c57e4cfd82781b46a979189699dd7b8386f0f')

package() {
  cd "$srcdir/deepseek-harness-desktop-${pkgver}"

  # Qt wrapper binary
  install -Dm755 bin/deepseek-harness-desktop \
    "$pkgdir/usr/lib/deepseek-harness-desktop/bin/deepseek-harness-desktop"
  install -dm755 "$pkgdir/usr/bin"
  ln -s ../lib/deepseek-harness-desktop/bin/deepseek-harness-desktop \
    "$pkgdir/usr/bin/deepseek-harness-desktop"

  # systemd user service for the dsh web backend (the wrapper enables it on
  # first run only when no equivalent service exists yet)
  install -Dm644 systemd/dsh-web.service \
    "$pkgdir/usr/lib/systemd/user/dsh-web.service"

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
