Dependencies
============

These are the dependencies currently used by Raven Core. You can find instructions for installing them in the `build-*.md` file for your platform.

| Dependency | Version used | Minimum required | CVEs | Shared | [Bundled Qt library](https://doc.qt.io/qt-5/configure-options.html) |
| --- | --- | --- | --- | --- | --- |
| Berkeley DB | [4.8.30](http://www.oracle.com/technetwork/database/database-technologies/berkeleydb/downloads/index.html) | 4.8.x | No |  |  |
| biplist | 0.9 | | | | |
| Boost | [1.71.0](http://www.boost.org/users/download/) | [1.47.0](https://github.com/bitcoin/bitcoin/pull/8920) | No |  |  |
| ccache | [3.3.4](https://ccache.samba.org/download.html) |  | No |  |  |
| cctools | | | | | |
| cdrkit | 1.1.11 | | | | |
| Clang | [11.0.1](http://llvm.org/releases/download.html) |  (C++11 support) |  |  |  |
| D-Bus | [1.10.18](https://cgit.freedesktop.org/dbus/dbus/tree/NEWS?h=dbus-1.10) |  | No | Yes |  |
| ds_store    | 1.3.0  | | | | |
| Expat | [2.4.1](https://libexpat.github.io/) |  | Yes | Yes |  |
| fontconfig | [2.12.1](https://www.freedesktop.org/software/fontconfig/release/) |  | No | Yes |  |
| FreeType | [2.7.1](http://download.savannah.gnu.org/releases/freetype) |  | No |  |  |
| GCC |  | [4.7+](https://gcc.gnu.org/) |  |  |  |
| HarfBuzz-NG |  |  |  |  |  |
| libdmg-hfsplus | | | | | |
| libevent | [2.1.12-stable](https://github.com/libevent/libevent/releases) | 2.0.22 | No |  |  |
| libICE    | 1.0.9  | | | | |
| libjpeg |  |  |  |  | [Yes](https://github.com/RavenProject/Ravencoin/blob/master/depends/packages/qt.mk#L75) |
| libpng |  |  |  |  | [Yes](https://github.com/RavenProject/Ravencoin/blob/master/depends/packages/qt.mk#L74) |
| libtapi    |   | | | | |
| libSM    | 1.2.2  | | | | |
| libX11    | 1.6.2  | | | | |
| libXau    | 1.0.8  | | | | |
| libXext    | 1.3.2  | | | | |
| mac_alias    | 2.2.0  | | | | |
| MiniUPnPc | [2.0.20170509](http://miniupnp.free.fr/files) |  | No |  |  |
| OpenSSL | [1.1.1k](https://www.openssl.org/source) |  | Yes |  |  |
| PCRE |  |  |  |  | [Yes](https://github.com/RavenProject/Ravencoin/blob/master/depends/packages/qt.mk#L76) |
| protobuf | [2.6.1](https://github.com/google/protobuf/releases) |  | No |  |  |
| Python (tests) |  | [3.4](https://www.python.org/downloads) |  |  |  |
| qrencode | [3.4.4](https://fukuchi.org/works/qrencode) |  | No |  |  |
| Qt | [5.12.11](https://download.qt.io/official_releases/qt/) | 4.7+ | No |  |  |
| XCB | 1.10 |  |  |  | [Yes](https://github.com/RavenProject/Ravencoin/blob/master/depends/packages/qt.mk#L94) (Linux only) |
| xkbcommon | 0.8.4 |  |  |  | [Yes](https://github.com/RavenProject/Ravencoin/blob/master/depends/packages/qt.mk#L93) (Linux only) |
| ZeroMQ | [4.1.5](https://github.com/zeromq/libzmq/releases) |  | No |  |  |
| zlib | [1.2.11](http://zlib.net/) |  |  |  | No |
