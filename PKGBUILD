# Maintainer: DeepSeek Harness desktop packaging
# Contributor: szy
#
# Source build of the Qt6 WebEngine wrapper. The `dsh` backend itself is a
# separate project and is pulled in from the AUR (`deepseek-harness-git` also
# builds from source; swap to `deepseek-harness-bin` for the npm binary).

pkgname=deepseek-harness-desktop
pkgver=0.2.0
pkgrel=1
pkgdesc="Desktop Qt6 (WebEngine) wrapper for the DeepSeek Harness Web GUI, built from source"
arch=('x86_64')
url="https://github.com/unnamed42/deepseek-harness-desktop"
license=('MIT')
depends=('qt6-webengine'
         'deepseek-harness-git'
         'hicolor-icon-theme')
makedepends=('cmake'
             'ninja'
             'qt6-declarative')  # provides qmlimportscanner used by qt_add_executable
conflicts=('deepseek-harness-bin-desktop')  # both install the same /usr/bin/deepseek-harness-desktop
provides=('deepseek-harness-desktop')
source=("${pkgname}::git+https://github.com/unnamed42/deepseek-harness-desktop.git#commit=d214a4f96fae3fde2a17ad6e56d975f85ace0cbf")
sha256sums=('SKIP')

build() {
  cd "${srcdir}/${pkgname}"
  cmake -S . -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build
}

check() {
  true  # no test suite shipped
}

package() {
  DESTDIR="${pkgdir}" cmake --install "${srcdir}/${pkgname}/build"

  # Convenience symlink in PATH
  install -dm755 "${pkgdir}/usr/bin"
  ln -s ../lib/deepseek-harness-desktop/bin/deepseek-harness-desktop \
    "${pkgdir}/usr/bin/deepseek-harness-desktop"

  # systemd user service for the dsh web backend (the wrapper enables it on
  # first run only when no equivalent service exists yet)
  install -Dm644 "${srcdir}/${pkgname}/packaging/dsh-web.service" \
    "${pkgdir}/usr/lib/systemd/user/dsh-web.service"

  # Desktop entry
  install -Dm644 "${srcdir}/${pkgname}/packaging/deepseek-harness-desktop.desktop" \
    "${pkgdir}/usr/share/applications/deepseek-harness-desktop.desktop"

  # Icons
  for size in 16 24 32 48 64 128 256; do
    install -Dm644 "${srcdir}/${pkgname}/resources/icons/app-${size}.png" \
      "${pkgdir}/usr/share/icons/hicolor/${size}x${size}/apps/deepseek-harness-desktop.png"
  done
  install -Dm644 "${srcdir}/${pkgname}/resources/icons/deepseek-harness-desktop.svg" \
    "${pkgdir}/usr/share/icons/hicolor/scalable/apps/deepseek-harness-desktop.svg"

  # License
  install -Dm644 "${srcdir}/${pkgname}/LICENSE" \
    "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
}