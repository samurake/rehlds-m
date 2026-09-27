FROM almalinux:9@sha256:3a3fa7f043b142bc8008c8b308d39b47d2c84008addcd52f9f9a7a82d2a90474

RUN dnf install -y gcc-c++ glibc-devel.i686 libstdc++-devel.x86_64 libstdc++-devel.i686 \
       cmake make git binutils file python3 libasan.i686 libubsan.i686 \
    && dnf clean all

WORKDIR /work
