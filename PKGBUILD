pkgname=python-hardview
pkgver=4.0.0
pkgrel=1
pkgdesc='Cross-platform hardware information and real-time monitoring library'
arch=('x86_64' 'aarch64')
url='https://github.com/Suraset-Khurasee/Uni-Hard'
license=('MIT')
depends=('python' 'python-pybind11' 'python-rich')
makedepends=('git' 'python-build' 'python-installer' 'python-setuptools' 'python-wheel')
source=("$pkgname::git+https://github.com/Suraset-Khurasee/Uni-Hard.git#branch=main")
sha256sums=('SKIP')

build() {
  cd "$srcdir/$pkgname"
  python -m build --wheel --no-isolation
}

package() {
  cd "$srcdir/$pkgname"
  python -m installer --destdir="$pkgdir" dist/*.whl
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
